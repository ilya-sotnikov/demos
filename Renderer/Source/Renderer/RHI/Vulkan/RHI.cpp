#include "../RHI.hpp"

#include "Common.hpp"

#include "../../../Arena.hpp"
#include "../../../Utils.hpp"
#include "../../../Math/Utils.hpp"
#include "../Pool.hpp"
#include "TypeConvert.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif

#include <spirv_cross/spirv_cross.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <SDL3/SDL_vulkan.h>

#include <vector>
#include <string.h>

#define SDL_PRINT_ERROR(functionName) \
    fprintf(stderr, "%s:%d: " functionName " failed: %s\n", __FILE__, __LINE__, SDL_GetError())

#define VK_CHECK_HANDLE(x) \
    do \
    { \
        const VkResult vulkanResultTmp_ = x; \
        if (vulkanResultTmp_ != VK_SUCCESS) \
        { \
            VK_CHECK_PRINT_ERROR(vulkanResultTmp_); \
            return {.idx = RHI::INVALID_HANDLE}; \
        } \
    } \
    while (0)

struct QueueInfo
{
    u32 familyIdx;
    u32 queueIdx;
    VkQueue queue;
};

struct Buffer
{
    VkBuffer buffer;
    void* mapped;
    VkDeviceAddress deviceAddress;
    VmaAllocation allocation;
};

struct BufferDescriptor
{
    VkBuffer buffer;
};

struct Texture
{
    VkImage image;
    VkImageView view;
    VmaAllocation allocation;
    RHI::TextureType type;
    RHI::Format format;
    U32Vec3 dimensions;
};

struct TextureView
{
    VkImageView view;
};

struct Swapchain
{
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    VkSurfaceFormatKHR surfaceFormat;
    std::vector<RHI::TextureHandle> textures;
    std::vector<VkSemaphore> readyToPresentSemaphores;
    u32 minImageCount;
};

struct CommandBuffer
{
    VkCommandBuffer commandBuffer;
    RHI::Queue queue;
    int frameIdx;
};

struct Shader
{
    VkShaderModule module;
    VkShaderStageFlagBits stage;
    u32 pushConstantSize;
    U32Vec3 localSize;
};

struct Pipeline
{
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorUpdateTemplate descriptorUpdateTemplate;
    u32 pushConstantSize;
    U32Vec3 localSize;
};

union VulkanDescriptorInfo
{
    VkDescriptorImageInfo image;
    VkDescriptorBufferInfo buffer;
    VkAccelerationStructureKHR accelerationStructure;

    VulkanDescriptorInfo() = default;

    VulkanDescriptorInfo(
        VkBuffer buffer,
        VkDeviceSize offset = 0,
        VkDeviceSize range = VK_WHOLE_SIZE
    )
        : buffer{buffer, offset, range}
    { }

    VulkanDescriptorInfo(
        VkImageView imageView,
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        VkSampler sampler = VK_NULL_HANDLE
    )
        : image{sampler, imageView, imageLayout}
    { }

    VulkanDescriptorInfo(VkSampler sampler)
        : image{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED}
    { }

    VulkanDescriptorInfo(VkAccelerationStructureKHR accelerationStructure)
        : accelerationStructure{accelerationStructure}
    { }
};

struct Context
{
    struct Frame
    {
        VkCommandPool commandPoolGraphics{};
        VkCommandPool commandPoolCompute{};
        VkSemaphore imageAcquireSemaphore{};
    } frames[RHI::FRAMES_IN_FLIGHT];

    Arena scratchArena{};
    VkSurfaceKHR surface{};
    VkInstance instance{};
    VkDevice device{};
    VkPhysicalDevice physicalDevice{};
    VmaAllocator vmaAllocator{};
    QueueInfo graphicsQueueInfo{};
    QueueInfo computeQueueInfo{};
    VkDescriptorSetLayout bindlessTexturesDescriptorSetLayout{};
    VkDescriptorSet bindlessTexturesDescriptorSet{};
    VkDescriptorPool bindlessTexturesDescriptorPool{};
    Pool<RHI::BufferHandle, Buffer> buffers{};
    Pool<RHI::TextureHandle, Texture> textures{};
    Pool<RHI::TextureDescriptorHandle, TextureView> texturesDescriptors{};
    Pool<RHI::SamplerHandle, VkSampler> samplers{};
    Pool<RHI::SemaphoreHandle, VkSemaphore> semaphores{};
    Pool<RHI::CommandBufferHandle, CommandBuffer> commandBuffers{};
    Pool<RHI::PipelineHandle, Pipeline> pipelines{};
    Swapchain swapchain{};
    int frameIdx{};
    u32 imageIdx{};
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE]{};
};

// I don't see any situation where duplicating this stuff would be useful in practice.
Context sCtx;

static bool ExtensionIsAvailable(const char* name, SliceArg<VkExtensionProperties> extensions)
{
    DEBUG_ASSERT(name);

    for (int i = 0; i < extensions.count; ++i)
    {
        if (!strcmp(name, extensions.data[i].extensionName))
        {
            return true;
        }
    }
    return false;
}

static void GetQueue(
    u32& familyIdx,
    u32& queueIdx,
    VkPhysicalDevice device,
    VkQueueFlagBits flags,
    VkQueueFlagBits notFlags = static_cast<VkQueueFlagBits>(0)
)
{
    DEBUG_ASSERT(device);

    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties2> queueFamilies(queueFamilyCount);
    for (u32 i = 0; i < queueFamilyCount; ++i)
    {
        queueFamilies[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(device, &queueFamilyCount, queueFamilies.data());

    for (u32 i = 0; i < queueFamilyCount; ++i)
    {
        if (queueFamilies[i].queueFamilyProperties.queueFlags & flags)
        {
            DEBUG_ASSERT(queueFamilies[i].queueFamilyProperties.queueCount > 0);
            familyIdx = i;
            queueIdx = 0;
            if ((notFlags != 0) && !(queueFamilies[i].queueFamilyProperties.queueFlags & notFlags))
            {
                break;
            }
        }
    }
}

static VkImageAspectFlags VkAspectFlagsFromFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

static bool CreateShader(
    Shader& shader,
    std::vector<VkDescriptorSetLayoutBinding>& descriptorSetLayoutBindings,
    VkDevice device,
    Slice<u8> bytecode
)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(bytecode.count > 0);

    const VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size_t(bytecode.count),
        .pCode = reinterpret_cast<const u32*>(bytecode.data),
    };

    if (vkCreateShaderModule(device, &createInfo, nullptr, &shader.module) != VK_SUCCESS)
    {
        fprintf(stderr, "vulkan: vkCreateShaderModule failed\n");
        return false;
    }

    // TODO: C API.
    spirv_cross::Compiler compiler{
        reinterpret_cast<const u32*>(bytecode.data),
        size_t(bytecode.count) / sizeof(u32)
    };

    const spirv_cross::SmallVector<spirv_cross::EntryPoint> entryPoints
        = compiler.get_entry_points_and_stages();

    // NOTE: AFAIK in dxc multiple entry points are buggy. Also, I've encountered a strange bug (?)
    // with multiple compute shader entry points in Nsight Graphics, when profiling, some samples
    // always end up in the first entry point in a file when in reality it should not execute at
    // all. Maybe PEBKAC, but for now I'll just always use 1 entry point per file.
    ASSERT(entryPoints.size() == 1);

    switch (entryPoints[0].execution_model)
    {
    case spirv_cross::ExecutionModelVertex:
        shader.stage = VK_SHADER_STAGE_VERTEX_BIT;
        break;
    case spirv_cross::ExecutionModelFragment:
        shader.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;
    case spirv_cross::ExecutionModelGLCompute:
        shader.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        break;
    case spirv_cross::ExecutionModelMeshEXT:
        shader.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
        break;
    case spirv_cross::ExecutionModelTaskEXT:
        shader.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
        break;
    default:
        fprintf(stderr, "unhandled shader execution model: %d", entryPoints[0].execution_model);
        return false;
    }

    // TODO: would probably fail when specified by specialization constants,
    // idk, not using them for it so far.
    shader.localSize = {
        compiler.get_execution_mode_argument(spirv_cross::ExecutionMode::ExecutionModeLocalSize, 0),
        compiler.get_execution_mode_argument(spirv_cross::ExecutionMode::ExecutionModeLocalSize, 1),
        compiler.get_execution_mode_argument(spirv_cross::ExecutionMode::ExecutionModeLocalSize, 2),
    };

    spirv_cross::ShaderResources shaderResources = compiler.get_shader_resources();

    for (const spirv_cross::Resource& r : shaderResources.separate_images)
    {
        if (compiler.get_decoration(r.id, spv::DecorationDescriptorSet) == 1)
        {
            // Skip descriptor set 1 (bindless textures).
            continue;
        }

        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.separate_samplers)
    {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.storage_images)
    {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.sampled_images)
    {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.uniform_buffers)
    {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.storage_buffers)
    {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.acceleration_structures)
    {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptorSetLayoutBindings.push_back(binding);
    }

    if (!shaderResources.push_constant_buffers.empty())
    {
        ASSERT(shaderResources.push_constant_buffers.size() == 1);

        const spirv_cross::SPIRType type
            = compiler.get_type(shaderResources.push_constant_buffers[0].base_type_id);
        const size_t size = compiler.get_declared_struct_size(type);

        if (size > size_t(RHI::PUSH_CONSTANTS_MAX_SIZE_BYTES))
        {
            fprintf(
                stderr,
                "vulkan: push constant size %zu > %d\n",
                size,
                RHI::PUSH_CONSTANTS_MAX_SIZE_BYTES
            );
            return false;
        }

        shader.pushConstantSize = u32(size);
    }

    return true;
}

static VkPipelineBindPoint GetPipelineBindPoint(const Pipeline& pipeline)
{
    return (pipeline.localSize.x > 0) && (pipeline.localSize.y > 0) && (pipeline.localSize.z > 0)
        ? VK_PIPELINE_BIND_POINT_COMPUTE
        : VK_PIPELINE_BIND_POINT_GRAPHICS;
};

static bool DebugNameObject(
    VkDevice device,
    VkObjectType objectType,
    u64 objectHandle,
    const char* objectName
)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(objectHandle);
    DEBUG_ASSERT(objectName);

    const VkDebugUtilsObjectNameInfoEXT nameInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = objectType,
        .objectHandle = objectHandle,
        .pObjectName = objectName,
    };

    (void)device;
    (void)nameInfo;

#ifdef RHI_ENABLE_DEBUG_UTILS
    VK_CHECK(vkSetDebugUtilsObjectNameEXT(device, &nameInfo));
#endif

    return true;
}

bool RHI::Create(SDL_Window* window)
{
    DEBUG_ASSERT(window);

    sCtx.scratchArena.Init(16'000'000);

    VK_CHECK(volkInitialize());

    // Instance.
    {
        u32 vulkanApiVersion = 0;
        VK_CHECK(vkEnumerateInstanceVersion(&vulkanApiVersion));
        if (vulkanApiVersion < VK_API_VERSION_1_4)
        {
            fprintf(stderr, "vulkan: API version 1.4 is required\n");
            return false;
        }

        const VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "None",
            .applicationVersion = 1,
            .pEngineName = "None",
            .engineVersion = 1,
            .apiVersion = VK_API_VERSION_1_4,
        };

        u32 sdlExtCount = 0;

        // Find the required KHR surface extensions.
        const char* const* const sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
        if (!sdlExts)
        {
            SDL_PRINT_ERROR("SDL_Vulkan_GetInstanceExtensions");
            return false;
        }

        std::vector<const char*> requiredExtensions{sdlExts, sdlExts + sdlExtCount};
#ifdef RHI_ENABLE_DEBUG_UTILS
        requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        u32 extCount = 0;
        VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr));
        std::vector<VkExtensionProperties> availableExts(extCount);
        VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extCount, availableExts.data()));

        for (const char* ext : requiredExtensions)
        {
            const bool result
                = ExtensionIsAvailable(ext, {availableExts.data(), int(availableExts.size())});
            if (!result)
            {
                fprintf(stderr, "required vulkan extension %s is unavailable\n", ext);
                return false;
            }
        }

        const VkInstanceCreateInfo instanceInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo,
            .enabledExtensionCount = u32(requiredExtensions.size()),
            .ppEnabledExtensionNames = requiredExtensions.data(),
        };

        VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &sCtx.instance));

        volkLoadInstanceOnly(sCtx.instance);
    }

    // Surface.
    if (!SDL_Vulkan_CreateSurface(window, sCtx.instance, nullptr, &sCtx.surface))
    {
        SDL_PRINT_ERROR("SDL_Vulkan_CreateSurface ");
        return false;
    }

    const char* const requiredDeviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    // Physical device.
    {
        u32 physicalDeviceCount = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(sCtx.instance, &physicalDeviceCount, nullptr));
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        VK_CHECK(
            vkEnumeratePhysicalDevices(sCtx.instance, &physicalDeviceCount, physicalDevices.data())
        );

        int physicalDeviceIndex = -1;
        for (u32 i = 0; i < physicalDeviceCount; ++i)
        {
            const VkPhysicalDevice physicalDevice = physicalDevices[i];

            VkPhysicalDeviceSubgroupProperties subgroupProperties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
            };

            VkPhysicalDeviceProperties2 physicalDeviceProperties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                .pNext = &subgroupProperties,
            };
            vkGetPhysicalDeviceProperties2(physicalDevice, &physicalDeviceProperties);
            const bool supportsVulkan13
                = physicalDeviceProperties.properties.apiVersion >= VK_API_VERSION_1_3;
            bool supportsSubgroup = true;
            {
                const VkSubgroupFeatureFlags flag = subgroupProperties.supportedOperations;
                supportsSubgroup &= !!(flag & VK_SUBGROUP_FEATURE_VOTE_BIT);
                supportsSubgroup &= !!(flag & VK_SUBGROUP_FEATURE_BALLOT_BIT);
            }

            // Graphics support.
            u32 queueFamilyPropertyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties2(
                physicalDevice,
                &queueFamilyPropertyCount,
                nullptr
            );
            std::vector<VkQueueFamilyProperties2> queueFamilyProperties(queueFamilyPropertyCount);
            for (u32 j = 0; j < queueFamilyPropertyCount; ++j)
            {
                queueFamilyProperties[j].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
            }
            vkGetPhysicalDeviceQueueFamilyProperties2(
                physicalDevice,
                &queueFamilyPropertyCount,
                queueFamilyProperties.data()
            );
            bool supportsGraphicsAndPresentation = false;
            bool supportsAsyncCompute = false;
            for (u32 j = 0; j < queueFamilyPropertyCount; ++j)
            {
                const VkQueueFlags flags
                    = queueFamilyProperties[j].queueFamilyProperties.queueFlags;
                if (flags & VK_QUEUE_GRAPHICS_BIT)
                {
                    VkBool32 surfaceSupported = VK_FALSE;
                    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(
                        physicalDevice,
                        j,
                        sCtx.surface,
                        &surfaceSupported
                    ));
                    if (surfaceSupported == VK_TRUE)
                    {
                        supportsGraphicsAndPresentation = true;
                    }
                }
                else if (flags & VK_QUEUE_COMPUTE_BIT)
                {
                    supportsAsyncCompute = true;
                }
            }

            // Required extensions.
            u32 deviceExtensionPropertyCount = 0;
            VK_CHECK(vkEnumerateDeviceExtensionProperties(
                physicalDevice,
                nullptr,
                &deviceExtensionPropertyCount,
                nullptr
            ));
            std::vector<VkExtensionProperties> extensionProperties(deviceExtensionPropertyCount);
            VK_CHECK(vkEnumerateDeviceExtensionProperties(
                physicalDevice,
                nullptr,
                &deviceExtensionPropertyCount,
                extensionProperties.data()
            ));
            bool supportsRequiredExtensions = true;
            for (size_t j = 0; j < ARRAY_SIZE(requiredDeviceExtensions); ++j)
            {
                const bool result = ExtensionIsAvailable(
                    requiredDeviceExtensions[j],
                    {extensionProperties.data(), int(extensionProperties.size())}
                );
                if (!result)
                {
                    supportsRequiredExtensions = false;
                    break;
                }
            }

            // Required features.
            VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR unifiedImageLayoutFeatures = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR,
            };
            VkPhysicalDeviceVulkan14Features vulkanFeatures14 = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
                .pNext = &unifiedImageLayoutFeatures,
            };
            VkPhysicalDeviceVulkan13Features vulkanFeatures13 = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                .pNext = &vulkanFeatures14,
            };
            VkPhysicalDeviceVulkan12Features vulkanFeatures12 = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                .pNext = &vulkanFeatures13,
            };
            VkPhysicalDeviceVulkan11Features vulkanFeatures11 = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                .pNext = &vulkanFeatures12,
            };
            VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &vulkanFeatures11,
            };

            vkGetPhysicalDeviceFeatures2(physicalDevice, &physicalDeviceFeatures);

            // TODO: maybe check out profiles, this is getting ridiculous.
            VkBool32 supportsRequiredFeatures = true;
            supportsRequiredFeatures
                &= physicalDeviceFeatures.features.vertexPipelineStoresAndAtomics;
            supportsRequiredFeatures &= vulkanFeatures14.pushDescriptor;
            supportsRequiredFeatures &= vulkanFeatures13.dynamicRendering;
            supportsRequiredFeatures &= vulkanFeatures13.synchronization2;
            supportsRequiredFeatures &= vulkanFeatures13.shaderDemoteToHelperInvocation;
            supportsRequiredFeatures &= vulkanFeatures12.scalarBlockLayout;
            supportsRequiredFeatures &= vulkanFeatures12.shaderInt8;
            supportsRequiredFeatures &= vulkanFeatures12.storageBuffer8BitAccess;
            supportsRequiredFeatures &= vulkanFeatures12.uniformAndStorageBuffer8BitAccess;
            supportsRequiredFeatures
                &= vulkanFeatures12.descriptorBindingSampledImageUpdateAfterBind;
            supportsRequiredFeatures &= vulkanFeatures12.descriptorBindingPartiallyBound;
            supportsRequiredFeatures &= vulkanFeatures12.descriptorBindingVariableDescriptorCount;
            supportsRequiredFeatures
                &= vulkanFeatures12.descriptorBindingStorageImageUpdateAfterBind;
            supportsRequiredFeatures &= vulkanFeatures12.shaderSampledImageArrayNonUniformIndexing;
            supportsRequiredFeatures &= vulkanFeatures12.runtimeDescriptorArray;
            supportsRequiredFeatures &= vulkanFeatures12.drawIndirectCount;
            supportsRequiredFeatures &= vulkanFeatures12.shaderFloat16;
            supportsRequiredFeatures &= vulkanFeatures12.samplerFilterMinmax;
            supportsRequiredFeatures &= vulkanFeatures12.timelineSemaphore;
            supportsRequiredFeatures &= vulkanFeatures11.shaderDrawParameters;
            supportsRequiredFeatures &= vulkanFeatures11.storagePushConstant16;
            supportsRequiredFeatures &= vulkanFeatures11.storageBuffer16BitAccess;
            supportsRequiredFeatures &= vulkanFeatures11.uniformAndStorageBuffer16BitAccess;
            supportsRequiredFeatures &= physicalDeviceFeatures.features.multiDrawIndirect;
            supportsRequiredFeatures &= physicalDeviceFeatures.features.fragmentStoresAndAtomics;
            supportsRequiredFeatures &= physicalDeviceFeatures.features.samplerAnisotropy;
            supportsRequiredFeatures &= physicalDeviceFeatures.features.sampleRateShading;
            supportsRequiredFeatures &= physicalDeviceFeatures.features.textureCompressionBC;
            supportsRequiredFeatures &= physicalDeviceFeatures.features.shaderInt16;
            supportsRequiredFeatures &= physicalDeviceFeatures.features.depthClamp;

            // TODO: remove, use mesh shaders instead. Performance pitfalls of SV_PrimitiveID:
            // Variable Rate Shading with Visibility Buffer Rendering, John Hable
            // https://advances.realtimerendering.com/s2024/#hable
            supportsRequiredFeatures &= physicalDeviceFeatures.features.geometryShader;

            if (!unifiedImageLayoutFeatures.unifiedImageLayouts)
            {
                fprintf(
                    stderr,
                    "vulkan: %s extension is not supported, performance may be suboptimal\n",
                    VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME
                );
            }

            bool deviceOk = true;
            deviceOk &= supportsVulkan13;
            deviceOk &= supportsGraphicsAndPresentation;
            deviceOk &= supportsAsyncCompute;
            deviceOk &= supportsRequiredExtensions;
            deviceOk &= bool(supportsRequiredFeatures);
            deviceOk &= supportsSubgroup;
            if (deviceOk)
            {
                physicalDeviceIndex = int(i);
                break;
            }
        }

        if (physicalDeviceIndex < 0)
        {
            fprintf(stderr, "No suitable physical device found\n");
            return false;
        }

        sCtx.physicalDevice = physicalDevices[size_t(physicalDeviceIndex)];

        VkPhysicalDeviceProperties2 properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        };
        vkGetPhysicalDeviceProperties2(sCtx.physicalDevice, &properties);

        Utils::strlcpy(sCtx.deviceName, properties.properties.deviceName, sizeof(sCtx.deviceName));
        printf("GPU: %s\n", sCtx.deviceName);
    }

    // Logical device, queue.
    {
        // Already checked when picking a physical device.
        QueueInfo graphicsQueueInfo{};
        GetQueue(
            graphicsQueueInfo.familyIdx,
            graphicsQueueInfo.queueIdx,
            sCtx.physicalDevice,
            VK_QUEUE_GRAPHICS_BIT
        );
        QueueInfo computeQueueInfo{};
        GetQueue(
            computeQueueInfo.familyIdx,
            computeQueueInfo.queueIdx,
            sCtx.physicalDevice,
            VK_QUEUE_COMPUTE_BIT,
            VK_QUEUE_GRAPHICS_BIT
        );

        // NOTE: did not find any info if I should enable it or just checking it's availability
        // should be enough (which is probably the case), but did it anyway.
        VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR unifiedImageLayoutFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR,
            .unifiedImageLayouts = VK_TRUE,
        };
        VkPhysicalDeviceVulkan14Features vulkanFeatures14 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
            .pNext = &unifiedImageLayoutFeatures,
            .pushDescriptor = VK_TRUE,
        };
        VkPhysicalDeviceVulkan13Features vulkanFeatures13 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &vulkanFeatures14,
            .shaderDemoteToHelperInvocation = VK_TRUE,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE,
        };
        VkPhysicalDeviceVulkan12Features vulkanFeatures12 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &vulkanFeatures13,
            .drawIndirectCount = VK_TRUE,
            .storageBuffer8BitAccess = VK_TRUE,
            .uniformAndStorageBuffer8BitAccess = VK_TRUE,
            .shaderFloat16 = VK_TRUE,
            .shaderInt8 = VK_TRUE,
            .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
            .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
            .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
            .descriptorBindingPartiallyBound = VK_TRUE,
            .descriptorBindingVariableDescriptorCount = VK_TRUE,
            .runtimeDescriptorArray = VK_TRUE,
            .samplerFilterMinmax = VK_TRUE,
            .scalarBlockLayout = VK_TRUE,
            .timelineSemaphore = VK_TRUE,
            .bufferDeviceAddress = VK_TRUE,
        };
        VkPhysicalDeviceVulkan11Features vulkanFeatures11 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &vulkanFeatures12,
            .storageBuffer16BitAccess = VK_TRUE,
            .uniformAndStorageBuffer16BitAccess = VK_TRUE,
            .storagePushConstant16 = VK_TRUE,
            .shaderDrawParameters = VK_TRUE,
        };
        const VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &vulkanFeatures11,
            .features = {
                // TODO: remove, use mesh shaders instead. Performance pitfalls of SV_PrimitiveID:
                // Variable Rate Shading with Visibility Buffer Rendering, John Hable
                // https://advances.realtimerendering.com/s2024/#hable
                .geometryShader = VK_TRUE,

                .sampleRateShading = VK_TRUE,
                .multiDrawIndirect = VK_TRUE,
                .depthClamp = VK_TRUE,
                .samplerAnisotropy = VK_TRUE,
                .vertexPipelineStoresAndAtomics = VK_TRUE,
                .fragmentStoresAndAtomics = VK_TRUE,
                .shaderInt64 = VK_TRUE,
                .shaderInt16 = VK_TRUE,
            },
        };

        const f32 queuePriority = 1.0f;

        const VkDeviceQueueCreateInfo deviceQueueInfos[] = {
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = graphicsQueueInfo.familyIdx,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            },
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = computeQueueInfo.familyIdx,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            },
        };

        const VkDeviceCreateInfo deviceInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &physicalDeviceFeatures,
            .queueCreateInfoCount = ARRAY_SIZE(deviceQueueInfos),
            .pQueueCreateInfos = deviceQueueInfos,
            .enabledExtensionCount = u32(ARRAY_SIZE(requiredDeviceExtensions)),
            .ppEnabledExtensionNames = requiredDeviceExtensions,
        };

        VK_CHECK(vkCreateDevice(sCtx.physicalDevice, &deviceInfo, nullptr, &sCtx.device));

        volkLoadDevice(sCtx.device);

        vkGetDeviceQueue(
            sCtx.device,
            graphicsQueueInfo.familyIdx,
            graphicsQueueInfo.queueIdx,
            &graphicsQueueInfo.queue
        );
        sCtx.graphicsQueueInfo = graphicsQueueInfo;

        vkGetDeviceQueue(
            sCtx.device,
            computeQueueInfo.familyIdx,
            computeQueueInfo.queueIdx,
            &computeQueueInfo.queue
        );
        sCtx.computeQueueInfo = computeQueueInfo;
    }

    // VMA.
    {
        VmaAllocatorCreateInfo vmaAllocatorInfo = {
            .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .physicalDevice = sCtx.physicalDevice,
            .device = sCtx.device,
            .instance = sCtx.instance,
            .vulkanApiVersion = VK_API_VERSION_1_4,
        };

        VmaVulkanFunctions vmaVulkanFunctions{};
        VK_CHECK(vmaImportVulkanFunctionsFromVolk(&vmaAllocatorInfo, &vmaVulkanFunctions));
        vmaAllocatorInfo.pVulkanFunctions = &vmaVulkanFunctions;

        VK_CHECK(vmaCreateAllocator(&vmaAllocatorInfo, &sCtx.vmaAllocator));
    }

    // Command pools.
    {
        VkCommandPoolCreateInfo cmdPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = sCtx.graphicsQueueInfo.familyIdx,
        };

        for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
        {
            cmdPoolInfo.queueFamilyIndex = sCtx.graphicsQueueInfo.familyIdx;
            VK_CHECK(vkCreateCommandPool(
                sCtx.device,
                &cmdPoolInfo,
                nullptr,
                &sCtx.frames[i].commandPoolGraphics
            ));

            cmdPoolInfo.queueFamilyIndex = sCtx.computeQueueInfo.familyIdx;
            VK_CHECK(vkCreateCommandPool(
                sCtx.device,
                &cmdPoolInfo,
                nullptr,
                &sCtx.frames[i].commandPoolCompute
            ));
        }
    }

    // Synchronization primitives.
    {
        const VkSemaphoreCreateInfo semaphoreInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
        {
            VK_CHECK(vkCreateSemaphore(
                sCtx.device,
                &semaphoreInfo,
                nullptr,
                &sCtx.frames[i].imageAcquireSemaphore
            ));
            (void)DebugNameObject(
                sCtx.device,
                VK_OBJECT_TYPE_SEMAPHORE,
                reinterpret_cast<u64>(sCtx.frames[i].imageAcquireSemaphore),
                "ImageAcquireSemaphore"
            );
        }
    }

    // Bindless texture descriptor set layout.
    {
        const VkDescriptorSetLayoutBinding layoutBinding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = RHI::MAX_BINDLESS_DESCRIPTOR_COUNT,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        };

        const VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
            | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

        const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = 1,
            .pBindingFlags = &bindingFlags,
        };

        const VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &bindingFlagsInfo,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = 1,
            .pBindings = &layoutBinding,
        };

        VK_CHECK(vkCreateDescriptorSetLayout(
            sCtx.device,
            &layoutInfo,
            nullptr,
            &sCtx.bindlessTexturesDescriptorSetLayout
        ));
    }

    // Descriptor pool, descriptor set.
    {
        const VkDescriptorPoolSize poolSizes[] = {
            {
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                RHI::MAX_BINDLESS_DESCRIPTOR_COUNT,
            },
        };

        const VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets = 1,
            .poolSizeCount = ARRAY_SIZE(poolSizes),
            .pPoolSizes = poolSizes,
        };
        VK_CHECK(vkCreateDescriptorPool(
            sCtx.device,
            &poolInfo,
            nullptr,
            &sCtx.bindlessTexturesDescriptorPool
        ));

        const VkDescriptorSetAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = sCtx.bindlessTexturesDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &sCtx.bindlessTexturesDescriptorSetLayout,
        };

        VK_CHECK(vkAllocateDescriptorSets(
            sCtx.device,
            &allocateInfo,
            &sCtx.bindlessTexturesDescriptorSet
        ));
    }

    return true;
}

void RHI::Destroy()
{
    (void)RHI::DeviceWaitIdle();

    for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroyCommandPool(sCtx.device, sCtx.frames[i].commandPoolCompute, nullptr);
        vkDestroyCommandPool(sCtx.device, sCtx.frames[i].commandPoolGraphics, nullptr);
        vkDestroySemaphore(sCtx.device, sCtx.frames[i].imageAcquireSemaphore, nullptr);
    }
    vkDestroyDescriptorSetLayout(sCtx.device, sCtx.bindlessTexturesDescriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(sCtx.device, sCtx.bindlessTexturesDescriptorPool, nullptr);

    vmaDestroyAllocator(sCtx.vmaAllocator);
    vkDestroyDevice(sCtx.device, nullptr);
    vkDestroySurfaceKHR(sCtx.instance, sCtx.surface, nullptr);
    vkDestroyInstance(sCtx.instance, nullptr);
    volkFinalize();
}

RHI::BufferHandle RHI::CreateBuffer(const BufferDesc&& desc)
{
    DEBUG_ASSERT(desc.size > 0);

    bool failed = true;

    Buffer buffer{};
    // TODO: relying on the behavior similar to free(nullptr), did not check if it works.
    // clang-format off
    DEFER(
        if (failed)
        {
            if (buffer.mapped)
            {
                vmaUnmapMemory(sCtx.vmaAllocator, buffer.allocation);
            }
            vmaDestroyBuffer(sCtx.vmaAllocator, buffer.buffer, buffer.allocation);
        }
    );
    // clang-format on

    const VkBufferUsageFlags usage = desc.type == RHI::MEMORY_TYPE_DEFAULT_UNIFORM
        ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    const u32 queueFamilyIndices[]
        = {sCtx.graphicsQueueInfo.familyIdx, sCtx.computeQueueInfo.familyIdx};

    const VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = u32(ARRAY_SIZE(queueFamilyIndices)),
        .pQueueFamilyIndices = queueFamilyIndices,
    };

    const VmaAllocationCreateInfo allocationInfo = {
        .requiredFlags = MemoryTypeToVk(desc.type),
    };

    if (desc.minAlignment > 0)
    {
        VK_CHECK_HANDLE(vmaCreateBufferWithAlignment(
            sCtx.vmaAllocator,
            &bufferInfo,
            &allocationInfo,
            desc.minAlignment,
            &buffer.buffer,
            &buffer.allocation,
            nullptr
        ));
    }
    else
    {
        VK_CHECK_HANDLE(vmaCreateBuffer(
            sCtx.vmaAllocator,
            &bufferInfo,
            &allocationInfo,
            &buffer.buffer,
            &buffer.allocation,
            nullptr
        ));
    }

    if (allocationInfo.requiredFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        VK_CHECK_HANDLE(vmaMapMemory(sCtx.vmaAllocator, buffer.allocation, &buffer.mapped));
    }

    const VkBufferDeviceAddressInfo addressInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer.buffer,
    };
    buffer.deviceAddress = vkGetBufferDeviceAddress(sCtx.device, &addressInfo);

    if (desc.debugName)
    {
        if (!DebugNameObject(
                sCtx.device,
                VK_OBJECT_TYPE_BUFFER,
                reinterpret_cast<u64>(buffer.buffer),
                desc.debugName
            ))
        {
            return BufferHandle::Invalid();
        }
    }

    failed = false;

    return sCtx.buffers.CreateHandle(buffer);
}

void* RHI::GetBufferHostPtr(BufferHandle handle)
{
    DEBUG_ASSERT(handle);

    return sCtx.buffers.GetPtr(handle)->mapped;
}

u64 RHI::GetBufferDevicePtr(BufferHandle handle)
{
    DEBUG_ASSERT(handle);

    return static_cast<u64>(sCtx.buffers.GetPtr(handle)->deviceAddress);
}

void RHI::UnmapBuffer(BufferHandle handle)
{
    DEBUG_ASSERT(handle);

    Buffer* const buffer = sCtx.buffers.GetPtr(handle);

    if (buffer->mapped)
    {
        vmaUnmapMemory(sCtx.vmaAllocator, buffer->allocation);
        buffer->mapped = nullptr;
    }
}

void RHI::DestroyBuffer(BufferHandle handle)
{
    if (!handle)
    {
        return;
    }

    Buffer* const buffer = sCtx.buffers.GetPtr(handle);

    if (buffer->mapped)
    {
        vmaUnmapMemory(sCtx.vmaAllocator, buffer->allocation);
        buffer->mapped = nullptr;
    }
    vmaDestroyBuffer(sCtx.vmaAllocator, buffer->buffer, buffer->allocation);
    buffer->buffer = VK_NULL_HANDLE;
    buffer->allocation = VK_NULL_HANDLE;

    sCtx.buffers.DestroyHandle(handle);
}

RHI::TextureHandle RHI::CreateTexture(const TextureDesc&& desc)
{
    DEBUG_ASSERT(desc.dimensions.x > 0);
    DEBUG_ASSERT(desc.dimensions.y > 0);
    DEBUG_ASSERT(desc.dimensions.z > 0);
    DEBUG_ASSERT(desc.mipCount > 0);
    DEBUG_ASSERT(desc.layerCount > 0);
    DEBUG_ASSERT(desc.format != RHI::FORMAT_UNDEFINED);

    bool failed = true;
    Texture texture{};
    // clang-format off
    DEFER(
        if (failed)
        {
            vkDestroyImageView(sCtx.device, texture.view, nullptr);
            vmaDestroyImage(sCtx.vmaAllocator, texture.image, texture.allocation);
        }
    );
    // clang-format on

    const VkImageType type = TextureTypeToVk(desc.type);
    const VkImageViewType viewType = TextureTypeToViewVk(desc.type);
    const VkImageUsageFlags usage = TextureUsageToVk(desc.usage);
    const VkFormat format = FormatToVk(desc.format);

    // NOTE: won't check format availability since vkCreateImage should fail in this case.

    const u32 queueFamilyIndices[]
        = {sCtx.graphicsQueueInfo.familyIdx, sCtx.computeQueueInfo.familyIdx};

    const VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = type,
        .format = format,
        .extent = {
            .width = desc.dimensions.x,
            .height = desc.dimensions.y,
            .depth = desc.dimensions.z,
        },
        .mipLevels = desc.mipCount,
        .arrayLayers = desc.layerCount,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = u32(ARRAY_SIZE(queueFamilyIndices)),
        .pQueueFamilyIndices = queueFamilyIndices,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    const VmaAllocationCreateInfo allocationInfo = {
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VK_CHECK_HANDLE(vmaCreateImage(
        sCtx.vmaAllocator,
        &imageInfo,
        &allocationInfo,
        &texture.image,
        &texture.allocation,
        nullptr
    ));

    const VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.image,
        .viewType = viewType,
        .format = format,
        .subresourceRange = {
            .aspectMask = VkAspectFlagsFromFormat(format),
            .levelCount = desc.mipCount,
            .layerCount = desc.layerCount,
        },
    };
    VK_CHECK_HANDLE(vkCreateImageView(sCtx.device, &viewInfo, nullptr, &texture.view));

    if (desc.debugName)
    {
        if (!DebugNameObject(
                sCtx.device,
                VK_OBJECT_TYPE_IMAGE,
                reinterpret_cast<u64>(texture.image),
                desc.debugName
            ))
        {
            return TextureHandle::Invalid();
        }

        if (!DebugNameObject(
                sCtx.device,
                VK_OBJECT_TYPE_IMAGE_VIEW,
                reinterpret_cast<u64>(texture.view),
                desc.debugName
            ))
        {
            return TextureHandle::Invalid();
        }
    }

    texture.type = desc.type;
    texture.format = desc.format;
    texture.dimensions = desc.dimensions;

    failed = false;

    return sCtx.textures.CreateHandle(texture);
}

void RHI::DestroyTexture(RHI::TextureHandle handle)
{
    if (!handle)
    {
        return;
    }

    Texture* const texture = sCtx.textures.GetPtr(handle);

    vkDestroyImageView(sCtx.device, texture->view, nullptr);
    texture->view = VK_NULL_HANDLE;
    vmaDestroyImage(sCtx.vmaAllocator, texture->image, texture->allocation);
    texture->allocation = VK_NULL_HANDLE;
    texture->image = VK_NULL_HANDLE;

    sCtx.textures.DestroyHandle(handle);
}

RHI::TextureDescriptorHandle RHI::CreateTextureDescriptor(const TextureDescriptorDesc&& desc)
{
    DEBUG_ASSERT(desc.textureHandle);

    const Texture* const texture = sCtx.textures.GetPtr(desc.textureHandle);

    bool failed = true;
    TextureView view{};
    // clang-format off
    DEFER(
        if (failed)
        {
            vkDestroyImageView(sCtx.device, view.view, nullptr);
        }
    );
    // clang-format on

    const VkFormat format = FormatToVk(texture->format);

    const VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->image,
        .viewType = TextureTypeToViewVk(desc.type),
        .format = format,
        .subresourceRange = {
            .aspectMask = VkAspectFlagsFromFormat(format),
            .baseMipLevel = desc.baseMip,
            .levelCount = desc.mipCount,
            .baseArrayLayer = desc.baseLayer,
            .layerCount = desc.layerCount,
        },
    };
    VK_CHECK_HANDLE(vkCreateImageView(sCtx.device, &viewInfo, nullptr, &view.view));

    failed = false;

    return sCtx.texturesDescriptors.CreateHandle(view);
}

void RHI::DestroyTextureDescriptor(RHI::TextureDescriptorHandle handle)
{
    if (!handle)
    {
        return;
    }

    TextureView* const view = sCtx.texturesDescriptors.GetPtr(handle);

    vkDestroyImageView(sCtx.device, view->view, nullptr);

    sCtx.texturesDescriptors.DestroyHandle(handle);
}

RHI::Format RHI::GetTextureFormat(TextureHandle handle)
{
    return sCtx.textures.GetPtr(handle)->format;
}

U32Vec3 RHI::GetTextureDimensions(TextureHandle handle)
{
    return sCtx.textures.GetPtr(handle)->dimensions;
}

void RHI::UpdateTextureDescriptorSet(RHI::TextureHandle handle, u32 dstArrayElement)
{
    DEBUG_ASSERT(handle);

    const VkDescriptorImageInfo imageInfo = {
        .imageView = sCtx.textures.GetPtr(handle)->view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    const VkWriteDescriptorSet writeSet = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = sCtx.bindlessTexturesDescriptorSet,
        .dstBinding = 0,
        .dstArrayElement = dstArrayElement,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &imageInfo,
    };

    vkUpdateDescriptorSets(sCtx.device, 1, &writeSet, 0, nullptr);
}

RHI::SamplerHandle RHI::CreateSampler(const RHI::SamplerDesc&& desc)
{
    const VkSamplerReductionModeCreateInfo reductionModeInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
        .reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN,
    };

    const VkSamplerCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = desc.reductionMode != RHI::SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE
            ? &reductionModeInfo
            : nullptr,
        .magFilter = FilterToVk(desc.magFilter),
        .minFilter = FilterToVk(desc.magFilter),
        .mipmapMode = SamplerMipmapModeToVk(desc.mipmapMode),
        .addressModeU = SamplerAddressModeToVk(desc.addressModeU),
        .addressModeV = SamplerAddressModeToVk(desc.addressModeV),
        .addressModeW = SamplerAddressModeToVk(desc.addressModeW),
        .anisotropyEnable = desc.anisotropyEnable,
        .maxAnisotropy = desc.maxAnisotropy,
        .compareEnable = desc.compareEnable,
        .compareOp = CompareOpToVk(desc.compareOp),
        .minLod = desc.minLod,
        .maxLod = desc.maxLod,
    };

    VkSampler sampler{};

    VK_CHECK_HANDLE(vkCreateSampler(sCtx.device, &createInfo, nullptr, &sampler));

    return sCtx.samplers.CreateHandle(sampler);
}

void RHI::DestroySampler(RHI::SamplerHandle handle)
{
    if (!handle)
    {
        return;
    }

    vkDestroySampler(sCtx.device, *sCtx.samplers.GetPtr(handle), nullptr);

    sCtx.samplers.DestroyHandle(handle);
}

RHI::SemaphoreHandle RHI::CreateSemaphore(u64 initialValue)
{
    const VkSemaphoreTypeCreateInfo timelineSemaphoreTypeInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = initialValue,
    };
    const VkSemaphoreCreateInfo timelineSemaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timelineSemaphoreTypeInfo,
    };

    VkSemaphore semaphore{};

    VK_CHECK_HANDLE(vkCreateSemaphore(sCtx.device, &timelineSemaphoreInfo, nullptr, &semaphore));

    return sCtx.semaphores.CreateHandle(semaphore);
}

void RHI::DestroySemaphore(RHI::SemaphoreHandle handle)
{
    if (!handle)
    {
        return;
    }

    vkDestroySemaphore(sCtx.device, *sCtx.semaphores.GetPtr(handle), nullptr);

    sCtx.semaphores.DestroyHandle(handle);
}

bool RHI::GetSemaphoreValue(RHI::SemaphoreHandle handle, u64& value)
{
    DEBUG_ASSERT(handle);

    VK_CHECK(vkGetSemaphoreCounterValue(sCtx.device, *sCtx.semaphores.GetPtr(handle), &value));

    return true;
}

bool RHI::WaitSemaphore(RHI::SemaphoreHandle handle, u64 value, u64 timeout)
{
    DEBUG_ASSERT(handle);

    const VkSemaphoreWaitInfo waitInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = sCtx.semaphores.GetPtr(handle),
        .pValues = &value,
    };

    VK_CHECK(vkWaitSemaphores(sCtx.device, &waitInfo, timeout));

    return true;
}

RHI::CommandBufferHandle RHI::CreateCommandBuffer(
    RHI::Queue queue,
    int frameInFlightIdx,
    const char* debugName
)
{
    DEBUG_ASSERT(frameInFlightIdx >= 0);
    DEBUG_ASSERT(frameInFlightIdx < RHI::FRAMES_IN_FLIGHT);

    const VkCommandPool pool = queue == QUEUE_GRAPHICS
        ? sCtx.frames[frameInFlightIdx].commandPoolGraphics
        : sCtx.frames[frameInFlightIdx].commandPoolCompute;

    const VkCommandBufferAllocateInfo cmdBufferAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    CommandBuffer cb{};
    VK_CHECK_HANDLE(
        vkAllocateCommandBuffers(sCtx.device, &cmdBufferAllocateInfo, &cb.commandBuffer)
    );

    cb.queue = queue;
    cb.frameIdx = frameInFlightIdx;

    if (debugName)
    {
        if (!DebugNameObject(
                sCtx.device,
                VK_OBJECT_TYPE_COMMAND_BUFFER,
                reinterpret_cast<u64>(cb.commandBuffer),
                debugName
            ))
        {
            return RHI::CommandBufferHandle::Invalid();
        }
    }

    return sCtx.commandBuffers.CreateHandle(cb);
}

void RHI::DestroyCommandBuffer(RHI::CommandBufferHandle handle)
{
    if (handle)
    {
        const CommandBuffer* const cb = sCtx.commandBuffers.GetPtr(handle);

        const VkCommandPool pool = cb->queue == QUEUE_GRAPHICS
            ? sCtx.frames[cb->frameIdx].commandPoolGraphics
            : sCtx.frames[cb->frameIdx].commandPoolCompute;

        vkFreeCommandBuffers(sCtx.device, pool, 1, &cb->commandBuffer);

        sCtx.commandBuffers.DestroyHandle(handle);
    }
}

bool RHI::BeginCommandBuffer(RHI::CommandBufferHandle handle)
{
    DEBUG_ASSERT(handle);

    const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(sCtx.commandBuffers.GetPtr(handle)->commandBuffer, &beginInfo));

    return true;
}

bool RHI::EndCommandBuffer(RHI::CommandBufferHandle handle)
{
    DEBUG_ASSERT(handle);

    VK_CHECK(vkEndCommandBuffer(sCtx.commandBuffers.GetPtr(handle)->commandBuffer));

    return true;
}

bool RHI::QueueSubmit(RHI::Queue queue, const SliceArg<QueueSubmitDesc>&& desc)
{
    DEBUG_ASSERT(desc.count > 0);

    Arena scratchArena = sCtx.scratchArena;

    VkSubmitInfo2* const submitInfos = scratchArena.AllocOrDie<VkSubmitInfo2>(desc.count);

    for (int i = 0; i < desc.count; ++i)
    {
        const CommandBuffer* const cb = sCtx.commandBuffers.GetPtr(desc[i].cb);

        // +1 for optional image acquire binary semaphore.
        VkSemaphoreSubmitInfo* const semWaitSubmitInfos
            = scratchArena.AllocOrDie<VkSemaphoreSubmitInfo>(desc[i].waitSemaphores.count + 1);

        int waitSemaphoreCount = desc[i].waitSemaphores.count;
        for (int j = 0; j < waitSemaphoreCount; ++j)
        {
            semWaitSubmitInfos[j] = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = *sCtx.semaphores.GetPtr(desc[i].waitSemaphores[j].semaphore),
                .value = desc[i].waitSemaphores[j].value,
                .stageMask = StageToVk(desc[i].waitSemaphores[j].stageMask),
            };
        };

        if (desc[i].waitForTextureAcquire)
        {
            semWaitSubmitInfos[waitSemaphoreCount++] = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = sCtx.frames[sCtx.frameIdx].imageAcquireSemaphore,
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        // +1 for optional present binary semaphore.
        VkSemaphoreSubmitInfo* const semSignalSubmitInfos
            = scratchArena.AllocOrDie<VkSemaphoreSubmitInfo>(desc[i].signalSemaphores.count + 1);

        int signalSemaphoreCount = desc[i].signalSemaphores.count;
        for (int j = 0; j < desc[i].signalSemaphores.count; ++j)
        {
            semSignalSubmitInfos[j] = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = *sCtx.semaphores.GetPtr(desc[i].signalSemaphores[j].semaphore),
                .value = desc[i].signalSemaphores[j].value,
                .stageMask = StageToVk(desc[i].signalSemaphores[j].stageMask),
            };
        };

        if (desc[i].signalReadyToPresent)
        {
            semSignalSubmitInfos[signalSemaphoreCount++] = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = sCtx.swapchain.readyToPresentSemaphores[sCtx.imageIdx],
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            };
        }

        // NOTE: to prevent a dangling pointer, since submitInfos takes a pointer to this.
        VkCommandBufferSubmitInfo* const cbSubmitInfo
            = scratchArena.AllocOrDie<VkCommandBufferSubmitInfo>(1);
        *cbSubmitInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cb->commandBuffer,
        };

        submitInfos[i] = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = u32(waitSemaphoreCount),
            .pWaitSemaphoreInfos = semWaitSubmitInfos,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = cbSubmitInfo,
            .signalSemaphoreInfoCount = u32(signalSemaphoreCount),
            .pSignalSemaphoreInfos = semSignalSubmitInfos,
        };
    }

    const VkQueue vkQueue
        = queue == QUEUE_GRAPHICS ? sCtx.graphicsQueueInfo.queue : sCtx.computeQueueInfo.queue;

    VK_CHECK(vkQueueSubmit2(vkQueue, u32(desc.count), submitInfos, VK_NULL_HANDLE));

    return true;
}

RHI::PipelineHandle RHI::CreateComputePipeline(const RHI::ComputePipelineDesc&& desc)
{
    DEBUG_ASSERT(desc.bytecode.count > 0);

    std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    descriptorSetLayoutBindings.reserve(32);
    Shader shader{};
    DEFER(vkDestroyShaderModule(sCtx.device, shader.module, nullptr));

    if (!CreateShader(shader, descriptorSetLayoutBindings, sCtx.device, desc.bytecode))
    {
        return RHI::PipelineHandle::Invalid();
    }

    // Remove duplicates.
    bool bindingUsed[32]{};
    std::vector<VkDescriptorSetLayoutBinding> uniqueDescriptorSetLayoutBindings(
        descriptorSetLayoutBindings.size()
    );
    size_t uniqueBindingCount = 0;
    for (const VkDescriptorSetLayoutBinding& b : descriptorSetLayoutBindings)
    {
        ASSERT(b.binding < ARRAY_SIZE(bindingUsed));
        if (!bindingUsed[b.binding])
        {
            uniqueDescriptorSetLayoutBindings[uniqueBindingCount] = b;
            bindingUsed[b.binding] = true;
            ++uniqueBindingCount;
        }
    }
    uniqueDescriptorSetLayoutBindings.resize(uniqueBindingCount);

    // Sort for stable indices.
    std::sort(
        uniqueDescriptorSetLayoutBindings.begin(),
        uniqueDescriptorSetLayoutBindings.end(),
        [](VkDescriptorSetLayoutBinding a, VkDescriptorSetLayoutBinding b)
        { return a.binding < b.binding; }
    );

    const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
        .bindingCount = u32(uniqueDescriptorSetLayoutBindings.size()),
        .pBindings = uniqueDescriptorSetLayoutBindings.data(),
    };

    bool failed = true;
    Pipeline pipeline{};
    // clang-format off
    DEFER(
        if (failed)
        {
            vkDestroyPipelineLayout(sCtx.device, pipeline.layout, nullptr);
            vkDestroyDescriptorSetLayout(sCtx.device, pipeline.descriptorSetLayout, nullptr);
            vkDestroyPipeline(sCtx.device, pipeline.pipeline, nullptr);
            vkDestroyDescriptorUpdateTemplate(
                sCtx.device,
                pipeline.descriptorUpdateTemplate,
                nullptr
            );
        }
    );
    // clang-format on

    VK_CHECK_HANDLE(vkCreateDescriptorSetLayout(
        sCtx.device,
        &descriptorSetLayoutInfo,
        nullptr,
        &pipeline.descriptorSetLayout
    ));

    VkPushConstantRange pushConstantRange{};
    const bool usesPushConstants = shader.pushConstantSize > 0;
    if (usesPushConstants)
    {
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.size = shader.pushConstantSize;
        pipeline.pushConstantSize = shader.pushConstantSize;
    }

    const VkDescriptorSetLayout setLayouts[2] = {
        pipeline.descriptorSetLayout,
        sCtx.bindlessTexturesDescriptorSetLayout,
    };

    const VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc.usesBindlessTextures ? 2u : 1u,
        .pSetLayouts = setLayouts,
        .pushConstantRangeCount = usesPushConstants ? 1u : 0u,
        .pPushConstantRanges = &pushConstantRange,
    };
    VK_CHECK_HANDLE(vkCreatePipelineLayout(sCtx.device, &layoutInfo, nullptr, &pipeline.layout));

    const VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = shader.stage,
            .module = shader.module,
            .pName = "Main",
        },
        .layout = pipeline.layout,
    };

    VK_CHECK_HANDLE(vkCreateComputePipelines(
        sCtx.device,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &pipeline.pipeline
    ));

    std::vector<VkDescriptorUpdateTemplateEntry> descriptorUpdateTemplateEntries(
        uniqueDescriptorSetLayoutBindings.size()
    );

    for (size_t i = 0; i < uniqueDescriptorSetLayoutBindings.size(); ++i)
    {
        const VkDescriptorUpdateTemplateEntry entry = {
            .dstBinding = uniqueDescriptorSetLayoutBindings[i].binding,
            .descriptorCount = 1,
            .descriptorType = uniqueDescriptorSetLayoutBindings[i].descriptorType,
            .offset = sizeof(VulkanDescriptorInfo) * i,
            .stride = sizeof(VulkanDescriptorInfo),
        };

        descriptorUpdateTemplateEntries[i] = entry;
    }

    const VkDescriptorUpdateTemplateCreateInfo descriptorUpdateTemplateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .descriptorUpdateEntryCount = u32(descriptorUpdateTemplateEntries.size()),
        .pDescriptorUpdateEntries = descriptorUpdateTemplateEntries.data(),
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE,
        .pipelineLayout = pipeline.layout,
    };

    if (!descriptorUpdateTemplateEntries.empty())
    {
        VK_CHECK_HANDLE(vkCreateDescriptorUpdateTemplate(
            sCtx.device,
            &descriptorUpdateTemplateInfo,
            nullptr,
            &pipeline.descriptorUpdateTemplate
        ));
    }

    if (desc.debugName)
    {
        if (!DebugNameObject(
                sCtx.device,
                VK_OBJECT_TYPE_PIPELINE,
                reinterpret_cast<u64>(pipeline.pipeline),
                desc.debugName
            ))
        {
            return RHI::PipelineHandle::Invalid();
        }
    }

    failed = false;

    pipeline.localSize = shader.localSize;

    return sCtx.pipelines.CreateHandle(pipeline);
}

RHI::PipelineHandle RHI::CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc&& desc)
{
    DEBUG_ASSERT(desc.bytecodes.count > 0);

    std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    descriptorSetLayoutBindings.reserve(32);
    std::vector<Shader> shaders(desc.bytecodes.count);

    bool failed = true;
    Pipeline pipeline{};

    // clang-format off
    DEFER(
        for (Shader& shader : shaders)
        {
            vkDestroyShaderModule(sCtx.device, shader.module, nullptr);
        }
        if (failed)
        {
            vkDestroyPipelineLayout(sCtx.device, pipeline.layout, nullptr);
            vkDestroyDescriptorSetLayout(sCtx.device, pipeline.descriptorSetLayout, nullptr);
            vkDestroyPipeline(sCtx.device, pipeline.pipeline, nullptr);
            vkDestroyDescriptorUpdateTemplate(
                sCtx.device,
                pipeline.descriptorUpdateTemplate,
                nullptr
            );
        }
    );
    // clang-format on

    for (int i = 0; i < desc.bytecodes.count; ++i)
    {
        if (!CreateShader(
                shaders[i],
                descriptorSetLayoutBindings,
                sCtx.device,
                *(desc.bytecodes.begin() + i)
            ))
        {
            return RHI::PipelineHandle::Invalid();
        }
    }

    u32 pushConstantSize = 0;
    for (Shader& shader : shaders)
    {
        ASSERT(
            (pushConstantSize == 0 || shader.pushConstantSize == 0)
            || (pushConstantSize == shader.pushConstantSize)
        );
        pushConstantSize = pushConstantSize == 0 ? shader.pushConstantSize : pushConstantSize;
    }

    // Remove duplicates.
    bool bindingUsed[32]{};
    std::vector<VkDescriptorSetLayoutBinding> uniqueDescriptorSetLayoutBindings(
        descriptorSetLayoutBindings.size()
    );
    size_t uniqueBindingCount = 0;
    for (const VkDescriptorSetLayoutBinding& b : descriptorSetLayoutBindings)
    {
        ASSERT(b.binding < ARRAY_SIZE(bindingUsed));
        if (!bindingUsed[b.binding])
        {
            uniqueDescriptorSetLayoutBindings[uniqueBindingCount] = b;
            bindingUsed[b.binding] = true;
            ++uniqueBindingCount;
        }
    }
    uniqueDescriptorSetLayoutBindings.resize(uniqueBindingCount);

    // Sort for stable indices.
    std::sort(
        uniqueDescriptorSetLayoutBindings.begin(),
        uniqueDescriptorSetLayoutBindings.end(),
        [](VkDescriptorSetLayoutBinding a, VkDescriptorSetLayoutBinding b)
        { return a.binding < b.binding; }
    );

    const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
        .bindingCount = u32(uniqueDescriptorSetLayoutBindings.size()),
        .pBindings = uniqueDescriptorSetLayoutBindings.data(),
    };

    VK_CHECK_HANDLE(vkCreateDescriptorSetLayout(
        sCtx.device,
        &descriptorSetLayoutInfo,
        nullptr,
        &pipeline.descriptorSetLayout
    ));

    VkPushConstantRange pushConstantRange{};
    const bool usesPushConstants = pushConstantSize > 0;
    if (usesPushConstants)
    {
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.size = pushConstantSize;
        pipeline.pushConstantSize = pushConstantSize;
    }

    const VkDescriptorSetLayout setLayouts[2] = {
        pipeline.descriptorSetLayout,
        sCtx.bindlessTexturesDescriptorSetLayout,
    };

    const VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc.usesBindlessTextures ? 2u : 1u,
        .pSetLayouts = setLayouts,
        .pushConstantRangeCount = usesPushConstants ? 1u : 0u,
        .pPushConstantRanges = &pushConstantRange,
    };
    VK_CHECK_HANDLE(vkCreatePipelineLayout(sCtx.device, &layoutInfo, nullptr, &pipeline.layout));

    std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfos(shaders.size());
    for (size_t i = 0; i < shaders.size(); ++i)
    {
        shaderStageInfos[i] = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = shaders[i].stage,
            .module = shaders[i].module,
            .pName = "Main",
        };
    }

    // TODO: well I have to choose if I care about the performance for this
    // function, if not then std::vector is fine.
    Arena scratchArena = sCtx.scratchArena;

    VkFormat* const colorAttachmentFormats
        = scratchArena.AllocOrDie<VkFormat>(desc.colorTargets.count);
    for (int i = 0; i < desc.colorTargets.count; ++i)
    {
        colorAttachmentFormats[i] = FormatToVk(desc.colorTargets[i].format);
    }

    const VkPipelineRenderingCreateInfo pipelineRenderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = u32(desc.colorTargets.count),
        .pColorAttachmentFormats = colorAttachmentFormats,
        .depthAttachmentFormat = FormatToVk(desc.depthFormat),
        .stencilAttachmentFormat = FormatToVk(desc.stencilFormat),
    };

    const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    const VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = PrimitiveTopologyToVk(desc.topology),
    };

    const VkPipelineViewportStateCreateInfo viewportInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = desc.depthClampEnable,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = CullToVk(desc.cull),
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    const VkPipelineMultisampleStateCreateInfo multisampleInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    const VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc.depthMask & RHI::DEPTH_READ_BIT,
        .depthWriteEnable = desc.depthMask & RHI::DEPTH_WRITE_BIT,
        .depthCompareOp = VK_COMPARE_OP_GREATER,
    };

    VkPipelineColorBlendAttachmentState* const colorBlendAttachments
        = scratchArena.AllocOrDie<VkPipelineColorBlendAttachmentState>(desc.colorTargets.count);

    for (int i = 0; i < desc.colorTargets.count; ++i)
    {
        const RHI::PipelineColorTarget& t = desc.colorTargets[i];
        colorBlendAttachments[i] = {
            .blendEnable = t.blendEnable,
            .srcColorBlendFactor = BlendFactorToVk(t.srcColorFactor),
            .dstColorBlendFactor = BlendFactorToVk(t.dstColorFactor),
            .colorBlendOp = BlendOpToVk(t.colorOp),
            .srcAlphaBlendFactor = BlendFactorToVk(t.dstAlphaFactor),
            .dstAlphaBlendFactor = BlendFactorToVk(t.dstAlphaFactor),
            .alphaBlendOp = BlendOpToVk(t.alphaOp),
            .colorWriteMask = ColorComponentToVk(t.colorComponentMask),
        };
    }

    const VkPipelineColorBlendStateCreateInfo colorBlendingInfo = {
        .attachmentCount = u32(desc.colorTargets.count),
        .pAttachments = colorBlendAttachments,
    };

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    const VkPipelineDynamicStateCreateInfo dynamicStateInfo
        = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
           .dynamicStateCount = u32(ARRAY_SIZE(dynamicStates)),
           .pDynamicStates = dynamicStates};

    const VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipelineRenderingInfo,
        .stageCount = u32(shaderStageInfos.size()),
        .pStages = shaderStageInfos.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssemblyInfo,
        .pViewportState = &viewportInfo,
        .pRasterizationState = &rasterizationInfo,
        .pMultisampleState = &multisampleInfo,
        .pDepthStencilState = &depthStencilInfo,
        .pColorBlendState = &colorBlendingInfo,
        .pDynamicState = &dynamicStateInfo,
        .layout = pipeline.layout,
    };

    VK_CHECK_HANDLE(vkCreateGraphicsPipelines(
        sCtx.device,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &pipeline.pipeline
    ));

    std::vector<VkDescriptorUpdateTemplateEntry> descriptorUpdateTemplateEntries(
        uniqueDescriptorSetLayoutBindings.size()
    );

    for (size_t i = 0; i < uniqueDescriptorSetLayoutBindings.size(); ++i)
    {
        const VkDescriptorUpdateTemplateEntry entry = {
            .dstBinding = uniqueDescriptorSetLayoutBindings[i].binding,
            .descriptorCount = 1,
            .descriptorType = uniqueDescriptorSetLayoutBindings[i].descriptorType,
            .offset = sizeof(VulkanDescriptorInfo) * i,
            .stride = sizeof(VulkanDescriptorInfo),
        };

        descriptorUpdateTemplateEntries[i] = entry;
    }

    const VkDescriptorUpdateTemplateCreateInfo descriptorUpdateTemplateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .descriptorUpdateEntryCount = u32(descriptorUpdateTemplateEntries.size()),
        .pDescriptorUpdateEntries = descriptorUpdateTemplateEntries.data(),
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pipelineLayout = pipeline.layout,
    };

    if (!descriptorUpdateTemplateEntries.empty())
    {
        VK_CHECK_HANDLE(vkCreateDescriptorUpdateTemplate(
            sCtx.device,
            &descriptorUpdateTemplateInfo,
            nullptr,
            &pipeline.descriptorUpdateTemplate
        ));
    }

    if (desc.debugName)
    {
        if (!DebugNameObject(
                sCtx.device,
                VK_OBJECT_TYPE_PIPELINE,
                reinterpret_cast<u64>(pipeline.pipeline),
                desc.debugName
            ))
        {
            return RHI::PipelineHandle::Invalid();
        }
    }

    failed = false;

    return sCtx.pipelines.CreateHandle(pipeline);
}

U32Vec3 RHI::GetPipelineLocalSize(RHI::PipelineHandle handle)
{
    DEBUG_ASSERT(handle);

    return sCtx.pipelines.GetPtr(handle)->localSize;
}

void RHI::DestroyPipeline(RHI::PipelineHandle handle)
{
    if (!handle)
    {
        return;
    }

    const Pipeline* const pipeline = sCtx.pipelines.GetPtr(handle);

    vkDestroyPipelineLayout(sCtx.device, pipeline->layout, nullptr);
    vkDestroyDescriptorSetLayout(sCtx.device, pipeline->descriptorSetLayout, nullptr);
    vkDestroyPipeline(sCtx.device, pipeline->pipeline, nullptr);
    vkDestroyDescriptorUpdateTemplate(sCtx.device, pipeline->descriptorUpdateTemplate, nullptr);

    sCtx.pipelines.DestroyHandle(handle);
}

void RHI::CmdBarrier(
    RHI::CommandBufferHandle cb,
    RHI::StageFlags srcStageMask,
    RHI::AccessFlags srcAccessMask,
    RHI::StageFlags dstStageMask,
    RHI::AccessFlags dstAccessMask
)
{
    DEBUG_ASSERT(cb);

    const VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = StageToVk(srcStageMask),
        .srcAccessMask = AccessToVk(srcAccessMask),
        .dstStageMask = StageToVk(dstStageMask),
        .dstAccessMask = AccessToVk(dstAccessMask),
    };

    const VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(sCtx.commandBuffers.GetPtr(cb)->commandBuffer, &dependencyInfo);
}

void RHI::CmdTextureBarrier(
    RHI::CommandBufferHandle handle,
    const SliceArg<RHI::TextureBarrierDesc>&& desc
)
{
    DEBUG_ASSERT(handle);
    DEBUG_ASSERT(desc.count > 0);

    Arena scratchArena = sCtx.scratchArena;

    VkImageMemoryBarrier2* const barriers
        = scratchArena.AllocOrDie<VkImageMemoryBarrier2>(desc.count);

    for (int i = 0; i < desc.count; ++i)
    {
        const RHI::TextureBarrierDesc& d = desc[i];

        const Texture* const texture = sCtx.textures.GetPtr(d.handle);

        barriers[i] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = StageToVk(d.srcStageMask),
            .srcAccessMask = AccessToVk(d.srcAccessMask),
            .dstStageMask = StageToVk(d.dstStageMask),
            .dstAccessMask = AccessToVk(d.dstAccessMask),
            .oldLayout = TextureLayoutToVk(d.oldLayout),
            .newLayout = TextureLayoutToVk(d.newLayout),
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture->image,
            .subresourceRange = {
                .aspectMask = VkAspectFlagsFromFormat(FormatToVk(texture->format)),
                .baseMipLevel = 0,
                .levelCount = d.mipCount,
                .baseArrayLayer = 0,
                .layerCount = d.layerCount,
            },
        };
    }

    const CommandBuffer* const cb = sCtx.commandBuffers.GetPtr(handle);

    const VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = u32(desc.count),
        .pImageMemoryBarriers = barriers,
    };
    vkCmdPipelineBarrier2(cb->commandBuffer, &dependencyInfo);
}

void RHI::CmdTextureInvalidateBarrier(
    CommandBufferHandle cb,
    StageFlags srcStageMask,
    AccessFlags srcAccessMask,
    StageFlags dstStageMask,
    AccessFlags dstAccessMask,
    const SliceArg<TextureHandle>&& textures
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(textures.count > 0);

    Arena scratchArena = sCtx.scratchArena;

    VkImageMemoryBarrier2* const barriers
        = scratchArena.AllocOrDie<VkImageMemoryBarrier2>(textures.count);

    for (int i = 0; i < textures.count; ++i)
    {
        const Texture* const texture = sCtx.textures.GetPtr(textures[i]);

        barriers[i] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = StageToVk(srcStageMask),
            .srcAccessMask = AccessToVk(srcAccessMask),
            .dstStageMask = StageToVk(dstStageMask),
            .dstAccessMask = AccessToVk(dstAccessMask),
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture->image,
            .subresourceRange = {
                .aspectMask = VkAspectFlagsFromFormat(FormatToVk(texture->format)),
                .baseMipLevel = 0,
                .levelCount = RHI::ALL_MIPS,
                .baseArrayLayer = 0,
                .layerCount = RHI::ALL_LAYERS,
            },
        };
    }

    const VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = u32(textures.count),
        .pImageMemoryBarriers = barriers,
    };

    vkCmdPipelineBarrier2(sCtx.commandBuffers.GetPtr(cb)->commandBuffer, &dependencyInfo);
}

void RHI::CmdCopyBufferToTexture(
    RHI::CommandBufferHandle cb,
    RHI::BufferHandle buffer,
    RHI::TextureHandle texture,
    const SliceArg<RHI::BufferTextureCopy>&& copyRegions
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(buffer);
    DEBUG_ASSERT(texture);
    DEBUG_ASSERT(copyRegions.count > 0);

    Arena scratchArena = sCtx.scratchArena;

    VkBufferImageCopy* const regions
        = scratchArena.AllocOrDie<VkBufferImageCopy>(copyRegions.count);

    const Texture* const tex = sCtx.textures.GetPtr(texture);

    for (int i = 0; i < copyRegions.count; ++i)
    {
        const RHI::BufferTextureCopy& c = copyRegions[i];
        regions[i] = VkBufferImageCopy{
            .bufferOffset = c.bufferOffset,
            .bufferRowLength = c.bufferRowLength,
            .bufferImageHeight = c.bufferTextureHeight,
            .imageSubresource = {
                .aspectMask = VkAspectFlagsFromFormat(FormatToVk(tex->format)),
                .mipLevel = c.textureSubresource.mipLevel,
                .baseArrayLayer = c.textureSubresource.baseArrayLayer,
                .layerCount = c.textureSubresource.layerCount,
            },
            .imageOffset = {c.textureOffset.x, c.textureOffset.y, c.textureOffset.z},
            .imageExtent = {c.textureDimensions.x, c.textureDimensions.y, c.textureDimensions.z},
        };
    }

    vkCmdCopyBufferToImage(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        sCtx.buffers.GetPtr(buffer)->buffer,
        tex->image,
        VK_IMAGE_LAYOUT_GENERAL,
        u32(copyRegions.count),
        regions
    );
}

void RHI::CmdFillBuffer(
    RHI::CommandBufferHandle cb,
    RHI::BufferHandle buffer,
    u64 offset,
    u64 size,
    u32 data
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(buffer);
    DEBUG_ASSERT(size > 0);

    vkCmdFillBuffer(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        sCtx.buffers.GetPtr(buffer)->buffer,
        offset,
        size,
        data
    );
}

void RHI::CmdBindPipeline(RHI::CommandBufferHandle cb, RHI::PipelineHandle pipeline)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(pipeline);

    const Pipeline* const vkPipeline = sCtx.pipelines.GetPtr(pipeline);

    vkCmdBindPipeline(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        GetPipelineBindPoint(*vkPipeline),
        vkPipeline->pipeline
    );
}

void RHI::CmdDispatch(RHI::CommandBufferHandle cb, U32Vec3 groupCount)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(groupCount.x > 0);
    DEBUG_ASSERT(groupCount.y > 0);
    DEBUG_ASSERT(groupCount.z > 0);

    vkCmdDispatch(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        groupCount.x,
        groupCount.y,
        groupCount.z
    );
}

void RHI::CmdPushDescriptors(
    RHI::CommandBufferHandle cb,
    RHI::PipelineHandle pipeline,
    const SliceArg<RHI::DescriptorInfo>&& descriptors
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(pipeline);

    Arena scratchArena = sCtx.scratchArena;

    VulkanDescriptorInfo* const infos
        = scratchArena.AllocOrDie<VulkanDescriptorInfo>(descriptors.count);

    for (int i = 0; i < descriptors.count; ++i)
    {
        const RHI::DescriptorInfo& d = descriptors[i];
        switch (d.type)
        {
        case RHI::DescriptorInfo::TYPE_TEXTURE:
            infos[i] = {sCtx.textures.GetPtr(d.resource.texture)->view};
            break;
        case RHI::DescriptorInfo::TYPE_TEXTURE_DESCRIPTOR:
            infos[i] = {sCtx.texturesDescriptors.GetPtr(d.resource.textureDescriptor)->view};
            break;
        case RHI::DescriptorInfo::TYPE_SAMPLER:
            infos[i] = {*sCtx.samplers.GetPtr(d.resource.sampler)};
            break;
        case RHI::DescriptorInfo::TYPE_BUFFER:
            infos[i] = {sCtx.buffers.GetPtr(d.resource.buffer)->buffer};
            break;
        }
    }

    const Pipeline* const vkPipeline = sCtx.pipelines.GetPtr(pipeline);

    vkCmdPushDescriptorSetWithTemplate(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        vkPipeline->descriptorUpdateTemplate,
        vkPipeline->layout,
        0,
        infos
    );
}

void RHI::CmdPushConstants(
    RHI::CommandBufferHandle cb,
    RHI::PipelineHandle pipeline,
    const void* data
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(pipeline);
    DEBUG_ASSERT(data);

    const Pipeline* const vkPipeline = sCtx.pipelines.GetPtr(pipeline);

    vkCmdPushConstants(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        vkPipeline->layout,
        VK_SHADER_STAGE_ALL,
        0,
        vkPipeline->pushConstantSize,
        data
    );
}

void RHI::CmdSetViewport(const RHI::SetViewportDesc&& desc)
{
    DEBUG_ASSERT(desc.cb);
    DEBUG_ASSERT(desc.width >= 0.0f);
    DEBUG_ASSERT(desc.minDepth >= 0.0f);
    DEBUG_ASSERT(desc.maxDepth >= 0.0f);

    const VkViewport viewport = {
        .x = desc.x,
        .y = desc.y,
        .width = desc.width,
        .height = desc.height,
        .minDepth = desc.minDepth,
        .maxDepth = desc.maxDepth,
    };

    vkCmdSetViewport(sCtx.commandBuffers.GetPtr(desc.cb)->commandBuffer, 0, 1, &viewport);
}

void RHI::CmdSetScissor(const RHI::SetScissorDesc&& desc)
{
    DEBUG_ASSERT(desc.cb);

    const VkRect2D scissor = {
        .offset = {desc.offset.x, desc.offset.y},
        .extent = {desc.extent.x, desc.extent.y},
    };

    vkCmdSetScissor(sCtx.commandBuffers.GetPtr(desc.cb)->commandBuffer, 0, 1, &scissor);
}

void RHI::CmdBeginRendering(const BeginRenderingDesc&& desc)
{
    DEBUG_ASSERT(desc.cb);

    Arena scratchArena = sCtx.scratchArena;

    VkRenderingAttachmentInfo* const renderingAttachmentInfos
        = scratchArena.AllocOrDie<VkRenderingAttachmentInfo>(desc.colorTargets.count);

    for (int i = 0; i < desc.colorTargets.count; ++i)
    {
        const RHI::Attachment& t = desc.colorTargets[i];

        renderingAttachmentInfos[i] = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView
            = t.attachment.type == RHI::Attachment::TextureOrDescriptorHandle::TYPE_TEXTURE
                ? sCtx.textures.GetPtr(t.attachment.texture)->view
                : sCtx.texturesDescriptors.GetPtr(t.attachment.descriptor)->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = AttachmentLoadOpToVk(t.loadOp),
            .storeOp = AttachmentStoreOpToVk(t.storeOp),
        };
    }

    const bool depthAttachmentExists
        = desc.depthTarget.attachment.type != RHI::Attachment::TextureOrDescriptorHandle::TYPE_NONE;

    VkRenderingAttachmentInfo depthAttachmentInfo{};

    if (depthAttachmentExists)
    {
        depthAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = desc.depthTarget.attachment.type
                    == RHI::Attachment::TextureOrDescriptorHandle::TYPE_TEXTURE
                ? sCtx.textures.GetPtr(desc.depthTarget.attachment.texture)->view
                : sCtx.texturesDescriptors.GetPtr(desc.depthTarget.attachment.descriptor)->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = AttachmentLoadOpToVk(desc.depthTarget.loadOp),
            .storeOp = AttachmentStoreOpToVk(desc.depthTarget.storeOp),
        };
    }

    const VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = {desc.offset.x, desc.offset.y},
            .extent = {desc.extent.x, desc.extent.y},
        },
        .layerCount = 1,
        .colorAttachmentCount = u32(desc.colorTargets.count),
        .pColorAttachments = renderingAttachmentInfos,
        .pDepthAttachment = depthAttachmentExists ? &depthAttachmentInfo : nullptr,
    };

    vkCmdBeginRendering(sCtx.commandBuffers.GetPtr(desc.cb)->commandBuffer, &renderingInfo);
}

void RHI::CmdEndRendering(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    vkCmdEndRendering(sCtx.commandBuffers.GetPtr(cb)->commandBuffer);
}

void RHI::CmdBindIndexBuffer(
    RHI::CommandBufferHandle cb,
    RHI::BufferHandle buffer,
    u64 offset,
    RHI::IndexType indexType
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(buffer);

    vkCmdBindIndexBuffer(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        sCtx.buffers.GetPtr(buffer)->buffer,
        offset,
        IndexTypeToVk(indexType)
    );
}

void RHI::CmdDraw(
    RHI::CommandBufferHandle cb,
    u32 vertexCount,
    u32 instanceCount,
    u32 firstVertex,
    u32 firstInstance
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(vertexCount > 0);
    DEBUG_ASSERT(instanceCount > 0);

    vkCmdDraw(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        vertexCount,
        instanceCount,
        firstVertex,
        firstInstance
    );
}

void RHI::CmdDrawIndexed(
    RHI::CommandBufferHandle cb,
    u32 indexCount,
    u32 instanceCount,
    u32 firstIndex,
    i32 vertexOffset,
    u32 firstInstance
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(indexCount > 0);
    DEBUG_ASSERT(instanceCount > 0);

    vkCmdDrawIndexed(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        indexCount,
        instanceCount,
        firstIndex,
        vertexOffset,
        firstInstance
    );
}

void RHI::CmdDrawIndirect(
    CommandBufferHandle cb,
    BufferHandle buffer,
    u64 offset,
    u32 drawCount,
    u32 stride
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(buffer);
    DEBUG_ASSERT(drawCount > 0);

    vkCmdDrawIndirect(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        sCtx.buffers.GetPtr(buffer)->buffer,
        offset,
        drawCount,
        stride
    );
}

void RHI::CmdDrawIndexedIndirectCount(
    RHI::CommandBufferHandle cb,
    RHI::BufferHandle buffer,
    u64 offset,
    RHI::BufferHandle countBuffer,
    u64 countBufferOffset,
    u32 maxDrawCount,
    u32 stride
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(buffer);
    DEBUG_ASSERT(countBuffer);
    DEBUG_ASSERT(maxDrawCount > 0);

    vkCmdDrawIndexedIndirectCount(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        sCtx.buffers.GetPtr(buffer)->buffer,
        offset,
        sCtx.buffers.GetPtr(countBuffer)->buffer,
        countBufferOffset,
        maxDrawCount,
        stride
    );
}

void RHI::CmdBindTextureDescriptorSet(CommandBufferHandle cb, RHI::PipelineHandle pipeline)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(pipeline);

    const Pipeline* const vkPipeline = sCtx.pipelines.GetPtr(pipeline);

    vkCmdBindDescriptorSets(
        sCtx.commandBuffers.GetPtr(cb)->commandBuffer,
        GetPipelineBindPoint(*vkPipeline),
        vkPipeline->layout,
        1,
        1,
        &sCtx.bindlessTexturesDescriptorSet,
        0,
        nullptr
    );
}

bool RHI::CreateSwapchain(U32Vec2 size)
{
    (void)RHI::DeviceWaitIdle();

    RHI::DestroySwapchain();

    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        sCtx.physicalDevice,
        sCtx.surface,
        &surfaceCapabilities
    ));
    if (surfaceCapabilities.currentExtent.width != UINT32_MAX)
    {
        sCtx.swapchain.extent = surfaceCapabilities.currentExtent;
    }
    else
    {
        sCtx.swapchain.extent.width = Clamp(
            size.x,
            surfaceCapabilities.minImageExtent.width,
            surfaceCapabilities.maxImageExtent.width
        );
        sCtx.swapchain.extent.height = Clamp(
            size.y,
            surfaceCapabilities.minImageExtent.height,
            surfaceCapabilities.maxImageExtent.height
        );
    }

    u32 surfaceFormatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        sCtx.physicalDevice,
        sCtx.surface,
        &surfaceFormatCount,
        nullptr
    );
    DEBUG_ASSERT(surfaceFormatCount > 0);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        sCtx.physicalDevice,
        sCtx.surface,
        &surfaceFormatCount,
        surfaceFormats.data()
    );

    // TODO: support HDR when this gets merged:
    // https://forums.developer.nvidia.com/t/vulkan-extensions-needed-for-hdr-is-missing/334268/13
    bool swapchainSurfaceFormatFound = false;

    for (u32 i = 0; i < surfaceFormatCount; ++i)
    {
        const VkFormat format = surfaceFormats[i].format;
        if ((format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM)
            && (surfaceFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
        {
            sCtx.swapchain.surfaceFormat = surfaceFormats[i];
            swapchainSurfaceFormatFound = true;
            break;
        }
    }
    if (!swapchainSurfaceFormatFound)
    {
        fprintf(stderr, "vulkan: failed to find a suitable swapchain surface format\n");
        return false;
    }

    sCtx.swapchain.minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    const u32 maxImageCount = surfaceCapabilities.maxImageCount;
    if (surfaceCapabilities.maxImageCount > 0 && maxImageCount < sCtx.swapchain.minImageCount)
    {
        sCtx.swapchain.minImageCount = maxImageCount;
    }

    // The spec guarantees that at least one bit will be set.
    VkCompositeAlphaFlagBitsKHR surfaceCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
    {
        surfaceCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    else if (
        surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
    )
    {
        surfaceCompositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    else if (
        surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
    )
    {
        surfaceCompositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }

    const VkSwapchainCreateInfoKHR swapchainInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = sCtx.surface,
        .minImageCount = sCtx.swapchain.minImageCount,
        .imageFormat = sCtx.swapchain.surfaceFormat.format,
        .imageColorSpace = sCtx.swapchain.surfaceFormat.colorSpace,
        .imageExtent = sCtx.swapchain.extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = surfaceCompositeAlpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };

    VK_CHECK(vkCreateSwapchainKHR(sCtx.device, &swapchainInfo, nullptr, &sCtx.swapchain.swapchain));

    u32 swapchainTextureCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(
        sCtx.device,
        sCtx.swapchain.swapchain,
        &swapchainTextureCount,
        nullptr
    ));
    std::vector<VkImage> images(swapchainTextureCount);
    VK_CHECK(vkGetSwapchainImagesKHR(
        sCtx.device,
        sCtx.swapchain.swapchain,
        &swapchainTextureCount,
        images.data()
    ));

    std::vector<Texture> textures(swapchainTextureCount);

    for (u32 i = 0; i < swapchainTextureCount; ++i)
    {
        textures[i].image = images[i];
        textures[i].type = RHI::TEXTURE_TYPE_2D;
        textures[i].format = FormatToRHI(sCtx.swapchain.surfaceFormat.format);
        textures[i].dimensions = {sCtx.swapchain.extent.width, sCtx.swapchain.extent.height, 1};
    }

    // Creating image views for every swapchain image.
    VkImageViewCreateInfo imageViewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = sCtx.swapchain.surfaceFormat.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    for (u32 i = 0; i < swapchainTextureCount; ++i)
    {
        imageViewInfo.image = textures[i].image;
        VkImageView imageView{};
        VK_CHECK(vkCreateImageView(sCtx.device, &imageViewInfo, nullptr, &imageView));
        textures[i].view = imageView;
    }

    sCtx.swapchain.textures.resize(swapchainTextureCount);
    for (u32 i = 0; i < swapchainTextureCount; ++i)
    {
        sCtx.swapchain.textures[i] = sCtx.textures.CreateHandle(textures[i]);
    }

    sCtx.swapchain.readyToPresentSemaphores.resize(swapchainTextureCount);
    for (u32 i = 0; i < swapchainTextureCount; ++i)
    {
        const VkSemaphoreCreateInfo semaphoreInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        VK_CHECK(vkCreateSemaphore(
            sCtx.device,
            &semaphoreInfo,
            nullptr,
            &sCtx.swapchain.readyToPresentSemaphores[i]
        ));
        (void)DebugNameObject(
            sCtx.device,
            VK_OBJECT_TYPE_SEMAPHORE,
            reinterpret_cast<u64>(sCtx.swapchain.readyToPresentSemaphores[i]),
            "ReadyToPresentSemaphore"
        );
    }

    return true;
}

void RHI::DestroySwapchain()
{
    (void)RHI::DeviceWaitIdle();

    for (size_t i = 0; i < sCtx.swapchain.textures.size(); ++i)
    {
        Texture* const t = sCtx.textures.GetPtr(sCtx.swapchain.textures[i]);
        vkDestroyImageView(sCtx.device, t->view, nullptr);
        vkDestroySemaphore(sCtx.device, sCtx.swapchain.readyToPresentSemaphores[i], nullptr);
        sCtx.textures.DestroyHandle(sCtx.swapchain.textures[i]);
    }
    sCtx.swapchain.textures.clear();
    vkDestroySwapchainKHR(sCtx.device, sCtx.swapchain.swapchain, nullptr);
    sCtx.swapchain.swapchain = VK_NULL_HANDLE;
}

RHI::SwapchainResult RHI::AcquireNextSwapchainTexture(RHI::TextureHandle& swapchainTextureHandle)
{
    const VkResult vulkanResult = vkAcquireNextImageKHR(
        sCtx.device,
        sCtx.swapchain.swapchain,
        1'000'000'000,
        sCtx.frames[sCtx.frameIdx].imageAcquireSemaphore,
        nullptr,
        &sCtx.imageIdx
    );

    swapchainTextureHandle = sCtx.swapchain.textures[sCtx.imageIdx];

    switch (vulkanResult)
    {
    case VK_SUCCESS:
        return RHI::SWAPCHAIN_SUCCESS;
    case VK_ERROR_OUT_OF_DATE_KHR:
        return RHI::SWAPCHAIN_OUT_OF_DATE;
    case VK_SUBOPTIMAL_KHR:
        return RHI::SWAPCHAIN_SUBOPTIMAL;
    default:
        return RHI::SWAPCHAIN_ERROR;
    }
}

RHI::TextureHandle RHI::GetSwapchainTexture(u32 idx)
{
    DEBUG_ASSERT(idx < sCtx.swapchain.textures.size());
    return sCtx.swapchain.textures[idx];
}

RHI::SwapchainResult RHI::QueuePresent(Queue queue)
{
    const VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sCtx.swapchain.readyToPresentSemaphores[sCtx.imageIdx],
        .swapchainCount = 1,
        .pSwapchains = &sCtx.swapchain.swapchain,
        .pImageIndices = &sCtx.imageIdx,
    };

    const VkQueue vkQueue
        = queue == RHI::QUEUE_GRAPHICS ? sCtx.graphicsQueueInfo.queue : sCtx.computeQueueInfo.queue;

    const VkResult vulkanResult = vkQueuePresentKHR(vkQueue, &presentInfo);
    switch (vulkanResult)
    {
    case VK_SUCCESS:
        return RHI::SWAPCHAIN_SUCCESS;
    case VK_ERROR_OUT_OF_DATE_KHR:
        return RHI::SWAPCHAIN_OUT_OF_DATE;
    case VK_SUBOPTIMAL_KHR:
        return RHI::SWAPCHAIN_SUBOPTIMAL;
    default:
        return RHI::SWAPCHAIN_ERROR;
    }
}

bool RHI::DeviceWaitIdle()
{
    VK_CHECK(vkDeviceWaitIdle(sCtx.device));
    return true;
}

bool RHI::QueueWaitIdle(RHI::Queue queue)
{
    VK_CHECK(vkQueueWaitIdle(
        queue == RHI::QUEUE_GRAPHICS ? sCtx.graphicsQueueInfo.queue : sCtx.computeQueueInfo.queue
    ));
    return true;
}

bool RHI::BeginNewFrame(int frameInFlightIdx)
{
    DEBUG_ASSERT(frameInFlightIdx >= 0);
    DEBUG_ASSERT(frameInFlightIdx < RHI::FRAMES_IN_FLIGHT);

    sCtx.frameIdx = frameInFlightIdx;

    VK_CHECK(vkResetCommandPool(sCtx.device, sCtx.frames[frameInFlightIdx].commandPoolCompute, 0));
    VK_CHECK(vkResetCommandPool(sCtx.device, sCtx.frames[frameInFlightIdx].commandPoolGraphics, 0));

    return true;
}
