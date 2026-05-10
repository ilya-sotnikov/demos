#include "Vulkan.hpp"

#include "RendererCommon.hpp"
#include "Shaders/SharedConfig.hlsli"

#include "../Utils.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif

#include <spirv_cross/spirv_cross.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <SDL3/SDL_vulkan.h>

#include <string.h>
#include <algorithm>

#define SDL_PRINT_ERROR(functionName) \
    fprintf(stderr, "%s:%d: " functionName " failed: %s\n", __FILE__, __LINE__, SDL_GetError())

struct FileData
{
    void* data;
    long size;
};

// gives ownership, call free()
static FileData FileRead(const char* path)
{
    DEBUG_ASSERT(path);

    FileData result{};

    FILE* const fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "%s: fopen %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    DEFER(fclose(fp));

    if (fseek(fp, 0, SEEK_END))
    {
        fprintf(stderr, "%s: fseek SEEK_END %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    const long fileSize = ftell(fp);
    if (fileSize == -1)
    {
        fprintf(stderr, "%s: ftell %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    if (fseek(fp, 0, SEEK_SET))
    {
        fprintf(stderr, "%s: fseek SEEK_SET %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }

    void* const res = malloc(size_t((fileSize + 1)) * sizeof(u8));
    if (!res)
    {
        fprintf(stderr, "%s: malloc failed (size %ld): %s\n", __func__, fileSize, strerror(errno));
        return result;
    }

    if (fread(res, sizeof(u8), size_t(fileSize), fp) != size_t(fileSize))
    {

        if (feof(fp))
        {
            fprintf(stderr, "%s: fread %s failed: EOF\n", __func__, path);
        }
        else if (ferror(fp))
        {
            fprintf(stderr, "%s: fread %s failed: %s\n", __func__, path, strerror(errno));
        }
        free(res);
        return result;
    }

    result.data = res;
    result.size = fileSize;

    return result;
}

static Vulkan::QueueInfo GetQueue(
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

    Vulkan::QueueInfo queueInfo = {
        .familyIdx = UINT32_MAX,
        .queueIdx = UINT32_MAX,
    };
    for (u32 i = 0; i < queueFamilyCount; ++i)
    {
        if (queueFamilies[i].queueFamilyProperties.queueFlags & flags)
        {
            DEBUG_ASSERT(queueFamilies[i].queueFamilyProperties.queueCount > 0);
            queueInfo.familyIdx = i;
            queueInfo.queueIdx = 0;
            // queueInfo.queue is set after creating a logical device and calling vkGetDeviceQueue.
            if ((notFlags != 0) && !(queueFamilies[i].queueFamilyProperties.queueFlags & notFlags))
            {
                break;
            }
        }
    }

    return queueInfo;
}

struct Shader
{
    VkShaderModule module;
    VkShaderStageFlagBits stage;
    u32 pushConstantSize;
};

static bool CreateShader(
    Shader& shader,
    std::vector<VkDescriptorSetLayoutBinding>& descriptorSetLayoutBindings,
    VkDevice device,
    const char* shaderPath
)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(shaderPath);

    FileData fileData = FileRead(shaderPath);
    if (!fileData.data)
    {
        return false;
    }
    DEFER(SAFE_FREE(fileData.data));

    const VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size_t(fileData.size),
        .pCode = static_cast<u32*>(fileData.data),
    };

    if (vkCreateShaderModule(device, &createInfo, nullptr, &shader.module) != VK_SUCCESS)
    {
        fprintf(stderr, "vulkan: vkCreateShaderModule failed for %s\n", shaderPath);
        return false;
    }

    // TODO: C API.
    spirv_cross::Compiler compiler{
        static_cast<u32*>(fileData.data),
        size_t(fileData.size) / sizeof(u32)
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

        shader.pushConstantSize = u32(size);
    }

    return true;
}

static void FillSpecializationInfo(
    VkSpecializationInfo& specializationInfo,
    std::vector<VkSpecializationMapEntry>& specializationMapEntries,
    const i32* specializationConstants,
    size_t specializationConstantCount
)
{
    DEBUG_ASSERT(specializationMapEntries.empty());

    specializationMapEntries.resize(specializationConstantCount);

    for (size_t i = 0; i < specializationConstantCount; ++i)
    {
        specializationMapEntries[i] = {u32(i), u32(i * sizeof(i32)), sizeof(i32)};
    }

    specializationInfo = {
        .mapEntryCount = u32(specializationMapEntries.size()),
        .pMapEntries = specializationMapEntries.data(),
        .dataSize = specializationConstantCount * sizeof(i32),
        .pData = specializationConstants,
    };
}

bool Vulkan::FindSupportedImageFormat(
    VkFormat& result,
    VkPhysicalDevice physicalDevice,
    VkImageUsageFlags usageFlags,
    std::initializer_list<VkFormat> formats
)
{
    for (VkFormat format : formats)
    {
        const VkPhysicalDeviceImageFormatInfo2 formatInfo = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
            .format = format,
            .type = VK_IMAGE_TYPE_2D,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usageFlags,
        };

        VkImageFormatProperties2 imageProperties = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        };

        if (vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, &formatInfo, &imageProperties)
            == VK_SUCCESS)
        {
            result = format;
            return true;
        }
    }

    return false;
}

bool Vulkan::ExtensionIsAvailable(const char* name, Slice<VkExtensionProperties> extensions)
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

VkMemoryBarrier2 Vulkan::MemoryBarrier(
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask
)
{
    const VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
    };

    return barrier;
}

VkBufferMemoryBarrier2 Vulkan::BufferMemoryBarrier(
    VkBuffer buffer,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask,
    VkDeviceSize size
)
{
    DEBUG_ASSERT(buffer);
    DEBUG_ASSERT(size > 0);

    const VkBufferMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .buffer = buffer,
        .size = size,
    };

    return barrier;
}

VkImageMemoryBarrier2 Vulkan::ImageMemoryBarrier(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask,
    VkImageAspectFlags aspectMask,
    u32 levelCount,
    u32 layerCount,
    u32 srcQueueFamilyIdx,
    u32 dstQueueFamilyIdx
)
{
    DEBUG_ASSERT(image);

    const VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = srcQueueFamilyIdx,
        .dstQueueFamilyIndex = dstQueueFamilyIdx,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspectMask,
            .levelCount = levelCount,
            .layerCount = layerCount,
        },
    };

    return barrier;
}

void Vulkan::CmdMemoryBarrier(VkCommandBuffer cb, std::initializer_list<VkMemoryBarrier2> barriers)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(barriers.size() > 0);

    const VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = u32(barriers.size()),
        .pMemoryBarriers = barriers.begin(),
    };
    vkCmdPipelineBarrier2(cb, &dependencyInfo);
}

void Vulkan::CmdBufferMemoryBarrier(
    VkCommandBuffer cb,
    std::initializer_list<VkBufferMemoryBarrier2> barriers
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(barriers.size() > 0);

    const VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = u32(barriers.size()),
        .pBufferMemoryBarriers = barriers.begin(),
    };
    vkCmdPipelineBarrier2(cb, &dependencyInfo);
}

void Vulkan::CmdImageMemoryBarrier(
    VkCommandBuffer cb,
    std::initializer_list<VkImageMemoryBarrier2> barriers
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(barriers.size() > 0);

    const VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = u32(barriers.size()),
        .pImageMemoryBarriers = barriers.begin(),
    };
    vkCmdPipelineBarrier2(cb, &dependencyInfo);
}

void Vulkan::CmdBarrier(
    VkCommandBuffer cb,
    std::initializer_list<VkMemoryBarrier2> memoryBarriers,
    std::initializer_list<VkBufferMemoryBarrier2> bufferMemoryBarriers,
    std::initializer_list<VkImageMemoryBarrier2> imageMemoryBarriers
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(
        (memoryBarriers.size() > 0) || (bufferMemoryBarriers.size() > 0)
        || (imageMemoryBarriers.size() > 0)
    );

    const VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = u32(memoryBarriers.size()),
        .pMemoryBarriers = memoryBarriers.begin(),
        .bufferMemoryBarrierCount = u32(bufferMemoryBarriers.size()),
        .pBufferMemoryBarriers = bufferMemoryBarriers.begin(),
        .imageMemoryBarrierCount = u32(imageMemoryBarriers.size()),
        .pImageMemoryBarriers = imageMemoryBarriers.begin(),
    };
    vkCmdPipelineBarrier2(cb, &dependencyInfo);
}

bool Vulkan::FindMemoryType(
    u32& memoryTypeIdx,
    VkPhysicalDevice physicalDevice,
    u32 typeFilter,
    VkMemoryPropertyFlags properties
)
{
    DEBUG_ASSERT(physicalDevice);

    VkPhysicalDeviceMemoryProperties2 memProp = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
    };
    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &memProp);

    ASSERT(memProp.memoryProperties.memoryTypeCount <= 32);

    for (u32 i = 0; i < memProp.memoryProperties.memoryTypeCount; ++i)
    {
        const bool typeIsAvailable = typeFilter & (1U << i);
        const bool propertyIsSet
            = (memProp.memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
        if (typeIsAvailable && propertyIsSet)
        {
            memoryTypeIdx = i;
            return true;
        }
    }

    return false;
}

void Vulkan::CmdPushDescriptors(
    VkCommandBuffer cb,
    const Vulkan::Pipeline& pipeline,
    std::initializer_list<Vulkan::DescriptorInfo> descriptorInfos
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(pipeline.pipeline);
    DEBUG_ASSERT(pipeline.descriptorSetLayout);
    DEBUG_ASSERT(pipeline.descriptorUpdateTemplate);
    DEBUG_ASSERT(pipeline.layout);
    DEBUG_ASSERT(descriptorInfos.size() > 0);

    vkCmdPushDescriptorSetWithTemplate(
        cb,
        pipeline.descriptorUpdateTemplate,
        pipeline.layout,
        0,
        descriptorInfos.begin()
    );
}

bool Vulkan::DebugNameObject(
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

#ifdef VULKAN_ENABLE_DEBUG_UTILS
    VK_CHECK(vkSetDebugUtilsObjectNameEXT(device, &nameInfo));
#endif

    return true;
}

bool Vulkan::Device::Create(VkSurfaceKHR& surface, SDL_Window* window)
{
    DEBUG_ASSERT(window);

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
#ifdef VULKAN_ENABLE_DEBUG_UTILS
        requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        u32 extCount = 0;
        VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr));
        std::vector<VkExtensionProperties> availableExts(extCount);
        VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extCount, availableExts.data()));

        for (const char* ext : requiredExtensions)
        {
            const bool result = Vulkan::ExtensionIsAvailable(
                ext,
                {availableExts.data(), int(availableExts.size())}
            );
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

        VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &mInstance));

        volkLoadInstanceOnly(mInstance);
    }

    // Surface.
    if (!SDL_Vulkan_CreateSurface(window, mInstance, nullptr, &surface))
    {
        SDL_PRINT_ERROR("SDL_Vulkan_CreateSurface ");
        return false;
    }

    const char* const requiredDeviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_MESH_SHADER_EXTENSION_NAME,
    };

    // Physical device.
    {
        u32 physicalDeviceCount = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(mInstance, &physicalDeviceCount, nullptr));
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        VK_CHECK(
            vkEnumeratePhysicalDevices(mInstance, &physicalDeviceCount, physicalDevices.data())
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
            for (u32 j = 0; j < queueFamilyPropertyCount; ++j)
            {
                if (queueFamilyProperties[j].queueFamilyProperties.queueFlags
                    & VK_QUEUE_GRAPHICS_BIT)
                {
                    VkBool32 surfaceSupported = VK_FALSE;
                    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(
                        physicalDevice,
                        j,
                        surface,
                        &surfaceSupported
                    ));
                    if (surfaceSupported == VK_TRUE)
                    {
                        supportsGraphicsAndPresentation = true;
                        break;
                    }
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
                const bool result = Vulkan::ExtensionIsAvailable(
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
            VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
            };
            VkPhysicalDeviceVulkan14Features vulkanFeatures14 = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
                .pNext = &meshShaderFeatures,
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
            supportsRequiredFeatures &= meshShaderFeatures.meshShader;

            bool deviceOk = true;
            deviceOk &= supportsVulkan13;
            deviceOk &= supportsGraphicsAndPresentation;
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

        mPhysicalDevice = physicalDevices[size_t(physicalDeviceIndex)];

        VkPhysicalDeviceProperties2 properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        };
        vkGetPhysicalDeviceProperties2(mPhysicalDevice, &properties);

        Utils::strlcpy(mGpuName, properties.properties.deviceName, sizeof(mGpuName));
        printf("GPU: %s\n", mGpuName);
    }

    // Logical device, queue.
    {
        // Already checked when picking a physical device.
        Vulkan::QueueInfo graphicsQueueInfo = GetQueue(mPhysicalDevice, VK_QUEUE_GRAPHICS_BIT);
        Vulkan::QueueInfo computeQueueInfo
            = GetQueue(mPhysicalDevice, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT);

        VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
            .meshShader = VK_TRUE,
        };
        VkPhysicalDeviceVulkan14Features vulkanFeatures14 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
            .pNext = &meshShaderFeatures,
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

        VK_CHECK(vkCreateDevice(mPhysicalDevice, &deviceInfo, nullptr, &mDevice));

        volkLoadDevice(mDevice);

        vkGetDeviceQueue(
            mDevice,
            graphicsQueueInfo.familyIdx,
            graphicsQueueInfo.queueIdx,
            &graphicsQueueInfo.queue
        );
        mGraphicsQueueInfo = graphicsQueueInfo;

        vkGetDeviceQueue(
            mDevice,
            computeQueueInfo.familyIdx,
            computeQueueInfo.queueIdx,
            &computeQueueInfo.queue
        );
        mComputeQueueInfo = computeQueueInfo;
    }

    // VMA.
    {
        VmaAllocatorCreateInfo vmaAllocatorInfo = {
            .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .physicalDevice = mPhysicalDevice,
            .device = mDevice,
            .instance = mInstance,
            .vulkanApiVersion = VK_API_VERSION_1_4,
        };

        VmaVulkanFunctions vmaVulkanFunctions{};
        VK_CHECK(vmaImportVulkanFunctionsFromVolk(&vmaAllocatorInfo, &vmaVulkanFunctions));
        vmaAllocatorInfo.pVulkanFunctions = &vmaVulkanFunctions;

        VK_CHECK(vmaCreateAllocator(&vmaAllocatorInfo, &mVmaAllocator));
    }

    return true;
}

void Vulkan::Device::Destroy()
{
    vmaDestroyAllocator(mVmaAllocator);
    vkDestroyDevice(mDevice, nullptr);
    vkDestroyInstance(mInstance, nullptr);
}

bool Vulkan::Device::CreateBuffer(const BufferDesc&& desc) const
{
    DEBUG_ASSERT(desc.size > 0);

    const VkBufferUsageFlags usage = desc.usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    const VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    const VmaAllocationCreateInfo allocationInfo = {
        .requiredFlags = desc.requiredFlags,
    };

    if (desc.minAlignment > 0)
    {
        VK_CHECK(vmaCreateBufferWithAlignment(
            mVmaAllocator,
            &bufferInfo,
            &allocationInfo,
            desc.minAlignment,
            &desc.buffer.buffer,
            &desc.buffer.allocation,
            nullptr
        ));
    }
    else
    {
        VK_CHECK(vmaCreateBuffer(
            mVmaAllocator,
            &bufferInfo,
            &allocationInfo,
            &desc.buffer.buffer,
            &desc.buffer.allocation,
            nullptr
        ));
    }

    if (desc.requiredFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        VK_CHECK(vmaMapMemory(mVmaAllocator, desc.buffer.allocation, &desc.buffer.mapped));
    }

    const VkBufferDeviceAddressInfo addressInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = desc.buffer.buffer,
    };
    desc.buffer.deviceAddress = vkGetBufferDeviceAddress(mDevice, &addressInfo);

    if (desc.debugName)
    {
        if (!Vulkan::DebugNameObject(
                mDevice,
                VK_OBJECT_TYPE_BUFFER,
                reinterpret_cast<u64>(desc.buffer.buffer),
                desc.debugName
            ))
        {
            return false;
        }
    }

    return true;
}

void Vulkan::Device::UnmapBuffer(Vulkan::Buffer& buffer) const
{
    if (buffer.mapped)
    {
        vmaUnmapMemory(mVmaAllocator, buffer.allocation);
        buffer.mapped = nullptr;
    }
}

void Vulkan::Device::DestroyBuffer(Vulkan::Buffer& buffer) const
{
    if (buffer.mapped)
    {
        vmaUnmapMemory(mVmaAllocator, buffer.allocation);
        buffer.mapped = nullptr;
    }
    vmaDestroyBuffer(mVmaAllocator, buffer.buffer, buffer.allocation);
    buffer.buffer = VK_NULL_HANDLE;
    buffer.allocation = VK_NULL_HANDLE;
}

bool Vulkan::Device::CreateImage(const ImageDesc&& desc) const
{
    DEBUG_ASSERT(desc.formats.size() > 0);
    DEBUG_ASSERT(desc.height > 0);
    DEBUG_ASSERT(desc.width > 0);
    DEBUG_ASSERT(desc.depth > 0);
    DEBUG_ASSERT(desc.mipLevels > 0);
    DEBUG_ASSERT(desc.arrayLayers > 0);

    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D;
    if (desc.depth > 1)
    {
        imageType = VK_IMAGE_TYPE_3D;
        imageViewType = VK_IMAGE_VIEW_TYPE_3D;
    }
    else if (desc.arrayLayers > 1)
    {
        imageViewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }

    VkFormat format{};
    if (!Vulkan::FindSupportedImageFormat(format, mPhysicalDevice, desc.usage, desc.formats))
    {
        return false;
    }

    const VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = imageType,
        .format = format,
        .extent = {
            .width = desc.width,
            .height = desc.height,
            .depth = desc.depth,
        },
        .mipLevels = desc.mipLevels,
        .arrayLayers = desc.arrayLayers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = desc.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    const VmaAllocationCreateInfo allocationInfo = {
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VK_CHECK(vmaCreateImage(
        mVmaAllocator,
        &imageInfo,
        &allocationInfo,
        &desc.image.image,
        &desc.image.allocation,
        nullptr
    ));

    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    switch (format)
    {
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM:
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        break;
    default:
        break;
    }

    const VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = desc.image.image,
        .viewType = imageViewType,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectMask,
            .levelCount = desc.mipLevels,
            .layerCount = desc.arrayLayers,
        },
    };
    VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &desc.image.view));

    if (desc.debugName)
    {
        if (!Vulkan::DebugNameObject(
                mDevice,
                VK_OBJECT_TYPE_IMAGE,
                reinterpret_cast<u64>(desc.image.image),
                desc.debugName
            ))
        {
            return false;
        }

        if (!Vulkan::DebugNameObject(
                mDevice,
                VK_OBJECT_TYPE_IMAGE_VIEW,
                reinterpret_cast<u64>(desc.image.view),
                desc.debugName
            ))
        {
            return false;
        }
    }

    desc.image.format = format;

    return true;
}

void Vulkan::Device::DestroyImage(Vulkan::Image& image) const
{
    vkDestroyImageView(mDevice, image.view, nullptr);
    image.view = VK_NULL_HANDLE;
    vmaDestroyImage(mVmaAllocator, image.image, image.allocation);
    image.allocation = VK_NULL_HANDLE;
    image.image = VK_NULL_HANDLE;
}

bool Vulkan::Device::CreateComputePipeline(const ComputePipelineDesc&& desc) const
{
    DEBUG_ASSERT(desc.shaderPath);

    std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    descriptorSetLayoutBindings.reserve(32);
    Shader shader{};
    DEFER(vkDestroyShaderModule(mDevice, shader.module, nullptr));

    if (!CreateShader(shader, descriptorSetLayoutBindings, mDevice, desc.shaderPath))
    {
        return false;
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

    VK_CHECK(vkCreateDescriptorSetLayout(
        mDevice,
        &descriptorSetLayoutInfo,
        nullptr,
        &desc.pipeline.descriptorSetLayout
    ));

    VkPushConstantRange pushConstantRange{};
    const bool usesPushConstants = shader.pushConstantSize > 0;
    if (usesPushConstants)
    {
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.size = shader.pushConstantSize;
        desc.pipeline.pushConstantSize = shader.pushConstantSize;
    }

    const VkDescriptorSetLayout setLayouts[2] = {
        desc.pipeline.descriptorSetLayout,
        desc.extraDescriptorSetLayout,
    };

    const VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc.extraDescriptorSetLayout ? 2u : 1u,
        .pSetLayouts = setLayouts,
        .pushConstantRangeCount = usesPushConstants ? 1u : 0u,
        .pPushConstantRanges = &pushConstantRange,
    };
    VK_CHECK(vkCreatePipelineLayout(mDevice, &layoutInfo, nullptr, &desc.pipeline.layout));

    VkSpecializationInfo specializationInfo{};
    std::vector<VkSpecializationMapEntry> specializationMapEntries;
    FillSpecializationInfo(
        specializationInfo,
        specializationMapEntries,
        desc.specializationConstants.begin(),
        desc.specializationConstants.size()
    );

    const VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = shader.stage,
            .module = shader.module,
            .pName = "Main",
            .pSpecializationInfo = &specializationInfo,
        },
        .layout = desc.pipeline.layout,
    };

    VK_CHECK(vkCreateComputePipelines(
        mDevice,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &desc.pipeline.pipeline
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
            .offset = sizeof(DescriptorInfo) * i,
            .stride = sizeof(DescriptorInfo),
        };

        descriptorUpdateTemplateEntries[i] = entry;
    }

    const VkDescriptorUpdateTemplateCreateInfo descriptorUpdateTemplateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .descriptorUpdateEntryCount = u32(descriptorUpdateTemplateEntries.size()),
        .pDescriptorUpdateEntries = descriptorUpdateTemplateEntries.data(),
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE,
        .pipelineLayout = desc.pipeline.layout,
    };

    if (!descriptorUpdateTemplateEntries.empty())
    {
        VK_CHECK(vkCreateDescriptorUpdateTemplate(
            mDevice,
            &descriptorUpdateTemplateInfo,
            nullptr,
            &desc.pipeline.descriptorUpdateTemplate
        ));
    }

    if (desc.debugName)
    {
        if (!Vulkan::DebugNameObject(
                mDevice,
                VK_OBJECT_TYPE_PIPELINE,
                reinterpret_cast<u64>(desc.pipeline.pipeline),
                desc.debugName
            ))
        {
            return false;
        }
    }

    return true;
}

bool Vulkan::Device::CreateGraphicsPipeline(const GraphicsPipelineDesc&& desc) const
{
    DEBUG_ASSERT(desc.shaderPaths.size() > 0);
    DEBUG_ASSERT(desc.colorAttachmentFormats.size() == desc.colorBlendAttachments.size());

    std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    descriptorSetLayoutBindings.reserve(32);
    std::vector<Shader> shaders(desc.shaderPaths.size());

    // clang-format off
    DEFER(
        for (Shader& shader : shaders)
        {
            vkDestroyShaderModule(mDevice, shader.module, nullptr);
        }
    );
    // clang-format on

    for (size_t i = 0; i < desc.shaderPaths.size(); ++i)
    {
        if (!CreateShader(
                shaders[i],
                descriptorSetLayoutBindings,
                mDevice,
                *(desc.shaderPaths.begin() + i)
            ))
        {
            return false;
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

    VK_CHECK(vkCreateDescriptorSetLayout(
        mDevice,
        &descriptorSetLayoutInfo,
        nullptr,
        &desc.pipeline.descriptorSetLayout
    ));

    VkPushConstantRange pushConstantRange{};
    const bool usesPushConstants = pushConstantSize > 0;
    if (usesPushConstants)
    {
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.size = pushConstantSize;
        desc.pipeline.pushConstantSize = pushConstantSize;
    }

    const VkDescriptorSetLayout setLayouts[2] = {
        desc.pipeline.descriptorSetLayout,
        desc.extraDescriptorSetLayout,
    };

    const VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc.extraDescriptorSetLayout ? 2u : 1u,
        .pSetLayouts = setLayouts,
        .pushConstantRangeCount = usesPushConstants ? 1u : 0u,
        .pPushConstantRanges = &pushConstantRange,
    };
    VK_CHECK(vkCreatePipelineLayout(mDevice, &layoutInfo, nullptr, &desc.pipeline.layout));

    VkSpecializationInfo specializationInfo{};
    std::vector<VkSpecializationMapEntry> specializationMapEntries;
    FillSpecializationInfo(
        specializationInfo,
        specializationMapEntries,
        desc.specializationConstants.begin(),
        desc.specializationConstants.size()
    );

    std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfos(shaders.size());
    for (size_t i = 0; i < shaders.size(); ++i)
    {
        shaderStageInfos[i] = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = shaders[i].stage,
            .module = shaders[i].module,
            .pName = "Main",
            .pSpecializationInfo = &specializationInfo,
        };
    }

    const VkPipelineRenderingCreateInfo pipelineRenderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = u32(desc.colorAttachmentFormats.size()),
        .pColorAttachmentFormats = desc.colorAttachmentFormats.begin(),
        .depthAttachmentFormat = desc.depthFormat,
        .stencilAttachmentFormat = desc.stencilFormat,
    };

    const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = u32(desc.vertexBindingDescriptions.size()),
        .pVertexBindingDescriptions = desc.vertexBindingDescriptions.begin(),
        .vertexAttributeDescriptionCount = u32(desc.vertexAttributeDescriptions.size()),
        .pVertexAttributeDescriptions = desc.vertexAttributeDescriptions.begin(),
    };

    const VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = desc.topology,
    };

    const VkPipelineViewportStateCreateInfo viewportInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = desc.viewportCount,
        .scissorCount = u32(desc.scissorCount),
    };

    const VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = desc.depthClampEnable,
        .rasterizerDiscardEnable = desc.rasterizerDiscardEnable,
        .polygonMode = desc.polygonMode,
        .cullMode = desc.cullMode,
        .frontFace = desc.frontFace,
        .lineWidth = desc.lineWidth,
    };

    const VkPipelineMultisampleStateCreateInfo multisampleInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    const VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc.depthTestEnable,
        .depthWriteEnable = desc.depthWriteEnable,
        .depthCompareOp = desc.depthCompareOp,
        .depthBoundsTestEnable = desc.depthBoundsTestEnable,
    };

    const VkPipelineColorBlendStateCreateInfo colorBlendingInfo = {

        .attachmentCount = u32(desc.colorBlendAttachments.size()),
        .pAttachments = desc.colorBlendAttachments.begin(),
    };

    const VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = u32(desc.dynamicStates.size()),
        .pDynamicStates = desc.dynamicStates.begin(),
    };

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
        .layout = desc.pipeline.layout,
    };

    VK_CHECK(vkCreateGraphicsPipelines(
        mDevice,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &desc.pipeline.pipeline
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
            .offset = sizeof(DescriptorInfo) * i,
            .stride = sizeof(DescriptorInfo),
        };

        descriptorUpdateTemplateEntries[i] = entry;
    }

    const VkDescriptorUpdateTemplateCreateInfo descriptorUpdateTemplateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .descriptorUpdateEntryCount = u32(descriptorUpdateTemplateEntries.size()),
        .pDescriptorUpdateEntries = descriptorUpdateTemplateEntries.data(),
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pipelineLayout = desc.pipeline.layout,
    };

    if (!descriptorUpdateTemplateEntries.empty())
    {
        VK_CHECK(vkCreateDescriptorUpdateTemplate(
            mDevice,
            &descriptorUpdateTemplateInfo,
            nullptr,
            &desc.pipeline.descriptorUpdateTemplate
        ));
    }

    if (desc.debugName)
    {
        if (!Vulkan::DebugNameObject(
                mDevice,
                VK_OBJECT_TYPE_PIPELINE,
                reinterpret_cast<u64>(desc.pipeline.pipeline),
                desc.debugName
            ))
        {
            return false;
        }
    }

    return true;
}

void Vulkan::Device::DestroyPipeline(Vulkan::Pipeline& pipeline) const
{
    vkDestroyPipelineLayout(mDevice, pipeline.layout, nullptr);
    pipeline.layout = VK_NULL_HANDLE;
    vkDestroyDescriptorSetLayout(mDevice, pipeline.descriptorSetLayout, nullptr);
    pipeline.descriptorSetLayout = VK_NULL_HANDLE;
    vkDestroyPipeline(mDevice, pipeline.pipeline, nullptr);
    pipeline.pipeline = VK_NULL_HANDLE;
    vkDestroyDescriptorUpdateTemplate(mDevice, pipeline.descriptorUpdateTemplate, nullptr);
    pipeline.descriptorUpdateTemplate = VK_NULL_HANDLE;
}

bool Vulkan::Device::QueueSubmit(const QueueSubmitDesc&& desc) const
{
    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = u32(desc.waitSemaphores.size()),
        .pWaitSemaphores = desc.waitSemaphores.begin(),
        .pWaitDstStageMask = &desc.waitDstStageMask,
        .commandBufferCount = 1,
        .pCommandBuffers = &desc.commandBuffer,
        .signalSemaphoreCount = u32(desc.signalSemaphores.size()),
        .pSignalSemaphores = desc.signalSemaphores.begin(),
    };

    VK_CHECK(vkQueueSubmit(desc.queueInfo.queue, 1, &submitInfo, desc.fence));

    return true;
}

VkResult Vulkan::Device::QueuePresent(const QueuePresentDesc&& desc) const
{
    const VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = u32(desc.waitSemaphores.size()),
        .pWaitSemaphores = desc.waitSemaphores.begin(),
        .swapchainCount = 1,
        .pSwapchains = &desc.swapchain,
        .pImageIndices = &desc.imageIdx,
    };
    return vkQueuePresentKHR(desc.queueInfo.queue, &presentInfo);
}

bool Vulkan::Device::QueueWaitIdle(QueueInfo queueInfo) const
{
    VK_CHECK(vkQueueWaitIdle(queueInfo.queue));
    return true;
}

bool Vulkan::Device::DeviceWaitIdle() const
{
    VK_CHECK(vkDeviceWaitIdle(mDevice));
    return true;
}
