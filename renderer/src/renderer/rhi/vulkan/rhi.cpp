#include "renderer/rhi/rhi.hpp"

#include "renderer/rhi/vulkan/common.hpp"

#include "arena.hpp"
#include "temp_allocator.hpp"
#include "containers.hpp"
#include "math/utils.hpp"
#include "renderer/rhi/pool.hpp"
#include "renderer/rhi/vulkan/type_convert.hpp"
#include "utils.hpp"

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
#include <EASTL/vector.h>
#include <EASTL/sort.h>

#define SDL_ERROR(function_name) \
    do { \
        fprintf( \
            stderr, \
            "%s:%d: " function_name " failed: %s\n", \
            __FILE__, \
            __LINE__, \
            SDL_GetError() \
        ); \
        exit(1); \
    } while (0)

#define VK_CHECK_HANDLE(x) \
    do { \
        const VkResult vulkan_result_tmp_ = x; \
        if (vulkan_result_tmp_ != VK_SUCCESS) { \
            VK_CHECK_PRINT_ERROR(vulkan_result_tmp_); \
            return {.idx = rhi::INVALID_HANDLE}; \
        } \
    } while (0)

struct QueueImpl {
    VkQueue queue;
    rhi::QueueType type;
    u32 idx;
};

struct BufferImpl {
    VkBuffer buffer;
    void* mapped;
    VkDeviceAddress device_address;
    VmaAllocation allocation;
};

struct BufferDescriptorImpl {
    VkBuffer buffer;
};

struct TextureImpl {
    VkImage image;
    VkImageView view;
    VmaAllocation allocation;
    rhi::TextureType type;
    rhi::Format format;
    Vec3U32 dimensions;
};

struct TextureViewImpl {
    VkImageView view;
};

struct SwapchainImpl {
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    VkSurfaceFormatKHR surface_format;
    eastl::vector<rhi::Texture> textures;
    eastl::vector<VkSemaphore> ready_to_present_semaphores;
    u32 min_image_count;
};

struct CommandBufferImpl {
    VkCommandBuffer command_buffer;
    VkCommandPool command_pool;
};

struct ShaderImpl {
    VkShaderModule module;
    VkShaderStageFlagBits stage;
    u32 push_constant_size;
    Vec3U32 local_size;
};

struct PipelineImpl {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorUpdateTemplate descriptor_update_template;
    u32 push_constant_size;
    Vec3U32 local_size;
};

union DescriptorInfoImpl {
    VkDescriptorImageInfo image;
    VkDescriptorBufferInfo buffer;
    VkAccelerationStructureKHR acceleration_structure;

    DescriptorInfoImpl() = default;

    DescriptorInfoImpl(
        VkBuffer buffer,
        VkDeviceSize offset = 0,
        VkDeviceSize range = VK_WHOLE_SIZE
    ):
        buffer{buffer, offset, range} {}

    DescriptorInfoImpl(
        VkImageView image_view,
        VkImageLayout image_layout = VK_IMAGE_LAYOUT_GENERAL,
        VkSampler sampler = VK_NULL_HANDLE
    ):
        image{sampler, image_view, image_layout} {}

    DescriptorInfoImpl(VkSampler sampler):
        image{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED} {}

    DescriptorInfoImpl(VkAccelerationStructureKHR acceleration_structure):
        acceleration_structure{acceleration_structure} {}
};

struct ContextImpl {
    struct Frame {
        VkSemaphore image_acquire_semaphore;
    } frames[rhi::FRAMES_IN_FLIGHT];

    VkSurfaceKHR surface;
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VmaAllocator vma_allocator;

    u32 graphics_queue_family_idx;
    eastl::vector<u32> graphics_queue_idxs; // TODO: Might as well count them?
    u32 compute_queue_family_idx;
    eastl::vector<u32> compute_queue_idxs;
    u32 transfer_queue_family_idx;
    eastl::vector<u32> transfer_queue_idxs;

    VkDescriptorSetLayout bindless_textures_descriptor_set_layout;
    VkDescriptorSet bindless_textures_descriptor_set;
    VkDescriptorPool bindless_textures_descriptor_pool;

    Pool<rhi::Buffer, BufferImpl> buffers;
    Pool<rhi::Texture, TextureImpl> textures;
    Pool<rhi::TextureDescriptor, TextureViewImpl> texture_descriptors;
    Pool<rhi::Sampler, VkSampler> samplers;
    Pool<rhi::Semaphore, VkSemaphore> semaphores;
    Pool<rhi::Queue, QueueImpl> queues;
    Pool<rhi::CommandPool, VkCommandPool> command_pools;
    Pool<rhi::CommandBuffer, CommandBufferImpl> command_buffers;
    Pool<rhi::Pipeline, PipelineImpl> pipelines;

    SwapchainImpl swapchain;

    int frame_idx;
    u32 image_idx;

    rhi::DeviceProperties device_properties;
};

// I don't see any situation where duplicating this stuff would be useful in practice.
ContextImpl s_ctx;

static bool extension_is_available(const char* name, SliceArg<VkExtensionProperties> extensions) {
    DEBUG_ASSERT(name);

    for (int i = 0; i < extensions.count; ++i) {
        if (!strcmp(name, extensions.data[i].extensionName)) {
            return true;
        }
    }
    return false;
}

static bool get_queue(
    u32& family_idx,
    u32& queue_count,
    VkPhysicalDevice device,
    VkQueueFlags flags,
    VkQueueFlags not_flags = 0
) {
    DEBUG_ASSERT(device);

    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(device, &queue_family_count, nullptr);
    eastl::vector<VkQueueFamilyProperties2> queue_families(queue_family_count);
    for (u32 i = 0; i < queue_family_count; ++i) {
        queue_families[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(device, &queue_family_count, queue_families.data());

    for (u32 i = 0; i < queue_family_count; ++i) {
        const VkQueueFamilyProperties props = queue_families[i].queueFamilyProperties;
        if (props.queueFlags & flags) {
            DEBUG_ASSERT(props.queueCount > 0);
            family_idx = i;
            queue_count = props.queueCount;
            if (not_flags == 0) {
                return true;
            } else if (!(props.queueFlags & not_flags)) {
                return true;
            }
        }
    }

    return false;
}

static bool find_queue_index(u32& idx, eastl::vector<u32>& idxs) {
    for (u32 i = 0; i < idxs.size(); ++i) {
        if (idxs[i] == 0) {
            idxs[i] = 1;
            idx = i;
            return true;
        }
    }

    return false;
}

static VkImageAspectFlags vk_aspect_flags_from_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

static bool create_shader(
    ShaderImpl& shader,
    eastl::vector<VkDescriptorSetLayoutBinding>& descriptor_set_layout_bindings,
    VkDevice device,
    Slice<u8> bytecode
) {
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(bytecode.count > 0);

    const VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size_t(bytecode.count),
        .pCode = reinterpret_cast<const u32*>(bytecode.data),
    };

    if (vkCreateShaderModule(device, &create_info, nullptr, &shader.module) != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateShaderModule failed\n");
        return false;
    }

    // TODO: C API.
    spirv_cross::Compiler compiler{
        reinterpret_cast<const u32*>(bytecode.data),
        size_t(bytecode.count) / sizeof(u32)
    };

    const spirv_cross::SmallVector<spirv_cross::EntryPoint> entry_points =
        compiler.get_entry_points_and_stages();

    // NOTE: AFAIK in dxc multiple entry points are buggy. Also, I've encountered a strange bug (?)
    // with multiple compute shader entry points in Nsight Graphics, when profiling, some samples
    // always end up in the first entry point in a file when in reality it should not execute at
    // all. Maybe PEBKAC, but for now I'll just always use 1 entry point per file.
    ASSERT(entry_points.size() == 1);

    switch (entry_points[0].execution_model) {
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
            fprintf(
                stderr,
                "unhandled shader execution model: %d",
                entry_points[0].execution_model
            );
            return false;
    }

    // TODO: would probably fail when specified by specialization constants,
    // idk, not using them for it so far.
    shader.local_size = {
        compiler.get_execution_mode_argument(spirv_cross::ExecutionMode::ExecutionModeLocalSize, 0),
        compiler.get_execution_mode_argument(spirv_cross::ExecutionMode::ExecutionModeLocalSize, 1),
        compiler.get_execution_mode_argument(spirv_cross::ExecutionMode::ExecutionModeLocalSize, 2),
    };

    spirv_cross::ShaderResources shader_resources = compiler.get_shader_resources();

    for (const spirv_cross::Resource& r: shader_resources.separate_images) {
        if (compiler.get_decoration(r.id, spv::DecorationDescriptorSet) == 1) {
            // Skip descriptor set 1 (bindless textures).
            continue;
        }

        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptor_set_layout_bindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r: shader_resources.separate_samplers) {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptor_set_layout_bindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r: shader_resources.storage_images) {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptor_set_layout_bindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r: shader_resources.sampled_images) {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptor_set_layout_bindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r: shader_resources.uniform_buffers) {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptor_set_layout_bindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r: shader_resources.storage_buffers) {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptor_set_layout_bindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r: shader_resources.acceleration_structures) {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = compiler.get_decoration(r.id, spv::DecorationBinding),
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

        descriptor_set_layout_bindings.push_back(binding);
    }

    if (!shader_resources.push_constant_buffers.empty()) {
        ASSERT(shader_resources.push_constant_buffers.size() == 1);

        const spirv_cross::SPIRType type =
            compiler.get_type(shader_resources.push_constant_buffers[0].base_type_id);
        const size_t size = compiler.get_declared_struct_size(type);

        if (size > size_t(rhi::PUSH_CONSTANTS_MAX_SIZE_BYTES)) {
            fprintf(
                stderr,
                "vulkan: push constant size %zu > %d\n",
                size,
                rhi::PUSH_CONSTANTS_MAX_SIZE_BYTES
            );
            return false;
        }

        shader.push_constant_size = u32(size);
    }

    return true;
}

static VkPipelineBindPoint get_pipeline_bind_point(const PipelineImpl& pipeline) {
    return (pipeline.local_size.x > 0) && (pipeline.local_size.y > 0) && (pipeline.local_size.z > 0)
        ? VK_PIPELINE_BIND_POINT_COMPUTE
        : VK_PIPELINE_BIND_POINT_GRAPHICS;
};

static void debug_name_object(
    VkDevice device,
    VkObjectType object_type,
    u64 object_handle,
    const char* object_name
) {
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(object_handle);
    DEBUG_ASSERT(object_name);

    const VkDebugUtilsObjectNameInfoEXT nameInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = object_type,
        .objectHandle = object_handle,
        .pObjectName = object_name,
    };

    (void)device;
    (void)nameInfo; // TODO: UNUSED macro.

#ifdef RHI_ENABLE_DEBUG_UTILS
    VK_CHECK(vkSetDebugUtilsObjectNameEXT(device, &nameInfo));
#endif
}

void rhi::create(SDL_Window* window) {
    DEBUG_ASSERT(window);

    VK_CHECK(volkInitialize());

    // Instance.
    {
        u32 vulkan_api_version = 0;
        VK_CHECK(vkEnumerateInstanceVersion(&vulkan_api_version));
        if (vulkan_api_version < VK_API_VERSION_1_4) {
            fprintf(stderr, "vulkan: API version 1.4 is required\n");
            ASSERT(0); // TODO: ASSERT_MSG.
        }

        const VkApplicationInfo app_info = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "None",
            .applicationVersion = 1,
            .pEngineName = "None",
            .engineVersion = 1,
            .apiVersion = VK_API_VERSION_1_4,
        };

        u32 sdl_ext_count = 0;

        // Find the required KHR surface extensions.
        const char* const* const sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
        if (!sdl_exts) {
            SDL_ERROR("SDL_Vulkan_GetInstanceExtensions");
        }

        eastl::vector<const char*> required_extensions{sdl_exts, sdl_exts + sdl_ext_count};
#ifdef RHI_ENABLE_DEBUG_UTILS
        required_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        u32 ext_count = 0;
        VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr));
        eastl::vector<VkExtensionProperties> available_exts(ext_count);
        VK_CHECK(
            vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, available_exts.data())
        );

        for (const char* ext: required_extensions) {
            const bool result =
                extension_is_available(ext, {available_exts.data(), available_exts.size()});
            if (!result) {
                fprintf(stderr, "required vulkan extension %s is unavailable\n", ext);
            }
        }

        const VkInstanceCreateInfo instance_info = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &app_info,
            .enabledExtensionCount = u32(required_extensions.size()),
            .ppEnabledExtensionNames = required_extensions.data(),
        };

        VK_CHECK(vkCreateInstance(&instance_info, nullptr, &s_ctx.instance));

        volkLoadInstanceOnly(s_ctx.instance);
    }

    // Surface.
    if (!SDL_Vulkan_CreateSurface(window, s_ctx.instance, nullptr, &s_ctx.surface)) {
        SDL_ERROR("SDL_Vulkan_CreateSurface ");
    }

    const char* const required_device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    // Physical device.
    {
        u32 physical_device_count = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(s_ctx.instance, &physical_device_count, nullptr));
        eastl::vector<VkPhysicalDevice> physical_devices(physical_device_count);
        VK_CHECK(vkEnumeratePhysicalDevices(
            s_ctx.instance,
            &physical_device_count,
            physical_devices.data()
        ));

        int physical_device_index = -1;
        for (u32 i = 0; i < physical_device_count; ++i) {
            const VkPhysicalDevice physical_device = physical_devices[i];

            VkPhysicalDeviceSubgroupProperties subgroup_properties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
            };

            VkPhysicalDeviceProperties2 physical_device_properties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                .pNext = &subgroup_properties,
            };
            vkGetPhysicalDeviceProperties2(physical_device, &physical_device_properties);
            const bool supports_vulkan13 =
                physical_device_properties.properties.apiVersion >= VK_API_VERSION_1_3;
            bool supports_subgroup = true;
            {
                const VkSubgroupFeatureFlags flag = subgroup_properties.supportedOperations;
                supports_subgroup &= !!(flag & VK_SUBGROUP_FEATURE_VOTE_BIT);
                supports_subgroup &= !!(flag & VK_SUBGROUP_FEATURE_BALLOT_BIT);
            }

            // Graphics support.
            u32 queue_family_property_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties2(
                physical_device,
                &queue_family_property_count,
                nullptr
            );
            eastl::vector<VkQueueFamilyProperties2> queue_family_properties(
                queue_family_property_count
            );
            for (u32 j = 0; j < queue_family_property_count; ++j) {
                queue_family_properties[j].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
            }
            vkGetPhysicalDeviceQueueFamilyProperties2(
                physical_device,
                &queue_family_property_count,
                queue_family_properties.data()
            );
            bool supports_graphics_and_presentation = false;
            bool supports_compute_queue = false;
            bool supports_transfer_queue = false;
            for (u32 j = 0; j < queue_family_property_count; ++j) {
                const VkQueueFlags flags =
                    queue_family_properties[j].queueFamilyProperties.queueFlags;

                if (flags & VK_QUEUE_GRAPHICS_BIT) {
                    VkBool32 surface_supported = VK_FALSE;
                    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(
                        physical_device,
                        j,
                        s_ctx.surface,
                        &surface_supported
                    ));
                    if (surface_supported == VK_TRUE) {
                        supports_graphics_and_presentation = true;
                    }
                } else if (flags & VK_QUEUE_COMPUTE_BIT) {
                    supports_compute_queue = true;
                } else if (flags & VK_QUEUE_TRANSFER_BIT) {
                    supports_transfer_queue = true;
                }
            }

            // Required extensions.
            u32 device_extension_property_count = 0;
            VK_CHECK(vkEnumerateDeviceExtensionProperties(
                physical_device,
                nullptr,
                &device_extension_property_count,
                nullptr
            ));
            eastl::vector<VkExtensionProperties> extension_properties(
                device_extension_property_count
            );
            VK_CHECK(vkEnumerateDeviceExtensionProperties(
                physical_device,
                nullptr,
                &device_extension_property_count,
                extension_properties.data()
            ));
            bool supports_required_extensions = true;
            for (size_t j = 0; j < ARRAY_SIZE(required_device_extensions); ++j) {
                const bool result = extension_is_available(
                    required_device_extensions[j],
                    {extension_properties.data(), extension_properties.size()}
                );
                if (!result) {
                    supports_required_extensions = false;
                    break;
                }
            }

            // Required features.
            VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR unified_image_layout_features = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR,
            };
            VkPhysicalDeviceVulkan14Features vulkan_features14 = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
                .pNext = &unified_image_layout_features,
            };
            VkPhysicalDeviceVulkan13Features vulkan_features13 = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                .pNext = &vulkan_features14,
            };
            VkPhysicalDeviceVulkan12Features vulkan_features12 = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                .pNext = &vulkan_features13,
            };
            VkPhysicalDeviceVulkan11Features vulkan_features11 = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                .pNext = &vulkan_features12,
            };
            VkPhysicalDeviceFeatures2 physical_device_features = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &vulkan_features11,
            };

            vkGetPhysicalDeviceFeatures2(physical_device, &physical_device_features);

            // TODO: maybe check out profiles, this is getting ridiculous.
            VkBool32 supports_required_features = true;
            supports_required_features &=
                physical_device_features.features.vertexPipelineStoresAndAtomics;
            supports_required_features &= vulkan_features14.pushDescriptor;
            supports_required_features &= vulkan_features13.dynamicRendering;
            supports_required_features &= vulkan_features13.synchronization2;
            supports_required_features &= vulkan_features13.shaderDemoteToHelperInvocation;
            supports_required_features &= vulkan_features12.scalarBlockLayout;
            supports_required_features &= vulkan_features12.shaderInt8;
            supports_required_features &= vulkan_features12.storageBuffer8BitAccess;
            supports_required_features &= vulkan_features12.uniformAndStorageBuffer8BitAccess;
            supports_required_features &=
                vulkan_features12.descriptorBindingSampledImageUpdateAfterBind;
            supports_required_features &= vulkan_features12.descriptorBindingPartiallyBound;
            supports_required_features &=
                vulkan_features12.descriptorBindingVariableDescriptorCount;
            supports_required_features &=
                vulkan_features12.descriptorBindingStorageImageUpdateAfterBind;
            supports_required_features &=
                vulkan_features12.shaderSampledImageArrayNonUniformIndexing;
            supports_required_features &= vulkan_features12.runtimeDescriptorArray;
            supports_required_features &= vulkan_features12.drawIndirectCount;
            supports_required_features &= vulkan_features12.shaderFloat16;
            supports_required_features &= vulkan_features12.samplerFilterMinmax;
            supports_required_features &= vulkan_features12.timelineSemaphore;
            supports_required_features &= vulkan_features11.shaderDrawParameters;
            supports_required_features &= vulkan_features11.storagePushConstant16;
            supports_required_features &= vulkan_features11.storageBuffer16BitAccess;
            supports_required_features &= vulkan_features11.uniformAndStorageBuffer16BitAccess;
            supports_required_features &= physical_device_features.features.multiDrawIndirect;
            supports_required_features &=
                physical_device_features.features.fragmentStoresAndAtomics;
            supports_required_features &= physical_device_features.features.samplerAnisotropy;
            supports_required_features &= physical_device_features.features.sampleRateShading;
            supports_required_features &= physical_device_features.features.textureCompressionBC;
            supports_required_features &= physical_device_features.features.shaderInt16;
            supports_required_features &= physical_device_features.features.depthClamp;

            // TODO: remove, use mesh shaders instead. Performance pitfalls of SV_PrimitiveID:
            // Variable Rate Shading with Visibility Buffer Rendering, John Hable
            // https://advances.realtimerendering.com/s2024/#hable
            supports_required_features &= physical_device_features.features.geometryShader;

            if (!unified_image_layout_features.unifiedImageLayouts) {
                fprintf(
                    stderr,
                    "vulkan: %s extension is not supported, performance may be suboptimal\n",
                    VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME
                );
            }

            bool device_ok = true;
            device_ok &= supports_vulkan13;
            device_ok &= supports_graphics_and_presentation;
            device_ok &= supports_compute_queue;
            device_ok &= supports_transfer_queue;
            device_ok &= supports_required_extensions;
            device_ok &= bool(supports_required_features);
            device_ok &= supports_subgroup;
            if (device_ok) {
                physical_device_index = int(i);
                break;
            }
        }

        if (physical_device_index < 0) {
            fprintf(stderr, "no suitable physical device found\n");
        }

        s_ctx.physical_device = physical_devices[size_t(physical_device_index)];

        VkPhysicalDeviceProperties2 properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        };
        vkGetPhysicalDeviceProperties2(s_ctx.physical_device, &properties);

        utils::strlcpy(
            s_ctx.device_properties.name,
            properties.properties.deviceName,
            sizeof(s_ctx.device_properties.name)
        );
        printf("GPU: %s\n", s_ctx.device_properties.name);

        s_ctx.device_properties.min_uniform_buffer_offset_alignment =
            properties.properties.limits.minUniformBufferOffsetAlignment;
    }

    // Logical device, queue.
    {
        // Already checked when picking a physical device.
        u32 graphics_queue_count = 0;
        bool result = get_queue(
            s_ctx.graphics_queue_family_idx,
            graphics_queue_count,
            s_ctx.physical_device,
            VK_QUEUE_GRAPHICS_BIT
        );
        ASSERT(result);
        s_ctx.graphics_queue_idxs.resize(graphics_queue_count);

        u32 compute_queue_count = 0;
        result = get_queue(
            s_ctx.compute_queue_family_idx,
            compute_queue_count,
            s_ctx.physical_device,
            VK_QUEUE_COMPUTE_BIT,
            VK_QUEUE_GRAPHICS_BIT
        );
        ASSERT(result);
        s_ctx.compute_queue_idxs.resize(compute_queue_count);

        u32 transfer_queue_count = 0;
        result = get_queue(
            s_ctx.transfer_queue_family_idx,
            transfer_queue_count,
            s_ctx.physical_device,
            VK_QUEUE_TRANSFER_BIT,
            VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT
        );
        ASSERT(result);
        s_ctx.transfer_queue_idxs.resize(transfer_queue_count);

        // NOTE: did not find any info if I should enable it or just checking it's availability
        // should be enough (which is probably the case), but did it anyway.
        VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR unified_image_layout_features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR,
            .unifiedImageLayouts = VK_TRUE,
        };
        VkPhysicalDeviceVulkan14Features vulkan_features14 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
            .pNext = &unified_image_layout_features,
            .pushDescriptor = VK_TRUE,
        };
        VkPhysicalDeviceVulkan13Features vulkan_features13 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &vulkan_features14,
            .shaderDemoteToHelperInvocation = VK_TRUE,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE,
        };
        VkPhysicalDeviceVulkan12Features vulkan_features12 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &vulkan_features13,
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
        VkPhysicalDeviceVulkan11Features vulkan_features11 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &vulkan_features12,
            .storageBuffer16BitAccess = VK_TRUE,
            .uniformAndStorageBuffer16BitAccess = VK_TRUE,
            .storagePushConstant16 = VK_TRUE,
            .shaderDrawParameters = VK_TRUE,
        };
        const VkPhysicalDeviceFeatures2 physical_device_features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &vulkan_features11,
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

        eastl::vector<f32> queue_priorities;
        queue_priorities.resize(
            math::max(
                s_ctx.graphics_queue_idxs.size(),
                s_ctx.compute_queue_idxs.size(),
                s_ctx.transfer_queue_idxs.size()
            )
        );
        for (f32& p: queue_priorities) {
            p = 1.0f;
        }

        const VkDeviceQueueCreateInfo device_queue_infos[] = {
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = s_ctx.graphics_queue_family_idx,
                .queueCount = u32(s_ctx.graphics_queue_idxs.size()),
                .pQueuePriorities = queue_priorities.data(),
            },
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = s_ctx.compute_queue_family_idx,
                .queueCount = u32(s_ctx.compute_queue_idxs.size()),
                .pQueuePriorities = queue_priorities.data(),
            },
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = s_ctx.transfer_queue_family_idx,
                .queueCount = u32(s_ctx.transfer_queue_idxs.size()),
                .pQueuePriorities = queue_priorities.data(),
            },
        };

        const VkDeviceCreateInfo device_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &physical_device_features,
            .queueCreateInfoCount = ARRAY_SIZE(device_queue_infos),
            .pQueueCreateInfos = device_queue_infos,
            .enabledExtensionCount = u32(ARRAY_SIZE(required_device_extensions)),
            .ppEnabledExtensionNames = required_device_extensions,
        };

        VK_CHECK(vkCreateDevice(s_ctx.physical_device, &device_info, nullptr, &s_ctx.device));

        volkLoadDevice(s_ctx.device);
    }

    // VMA.
    {
        VmaAllocatorCreateInfo vma_allocator_info = {
            .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .physicalDevice = s_ctx.physical_device,
            .device = s_ctx.device,
            .instance = s_ctx.instance,
            .vulkanApiVersion = VK_API_VERSION_1_4,
        };

        VmaVulkanFunctions vma_vulkan_functions{};
        VK_CHECK(vmaImportVulkanFunctionsFromVolk(&vma_allocator_info, &vma_vulkan_functions));
        vma_allocator_info.pVulkanFunctions = &vma_vulkan_functions;

        VK_CHECK(vmaCreateAllocator(&vma_allocator_info, &s_ctx.vma_allocator));
    }

    // Synchronization primitives.
    {
        const VkSemaphoreCreateInfo semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i) {
            VK_CHECK(vkCreateSemaphore(
                s_ctx.device,
                &semaphore_info,
                nullptr,
                &s_ctx.frames[i].image_acquire_semaphore
            ));
            debug_name_object(
                s_ctx.device,
                VK_OBJECT_TYPE_SEMAPHORE,
                reinterpret_cast<u64>(s_ctx.frames[i].image_acquire_semaphore),
                "image_acquire_semaphore"
            );
        }
    }

    // Bindless texture descriptor set layout.
    {
        const VkDescriptorSetLayoutBinding layout_binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = rhi::MAX_BINDLESS_DESCRIPTOR_COUNT,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        };

        const VkDescriptorBindingFlags binding_flags =
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

        const VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = 1,
            .pBindingFlags = &binding_flags,
        };

        const VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &binding_flags_info,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = 1,
            .pBindings = &layout_binding,
        };

        VK_CHECK(vkCreateDescriptorSetLayout(
            s_ctx.device,
            &layoutInfo,
            nullptr,
            &s_ctx.bindless_textures_descriptor_set_layout
        ));
    }

    // Descriptor pool, descriptor set.
    {
        const VkDescriptorPoolSize pool_sizes[] = {
            {
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                rhi::MAX_BINDLESS_DESCRIPTOR_COUNT,
            },
        };

        const VkDescriptorPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets = 1,
            .poolSizeCount = ARRAY_SIZE(pool_sizes),
            .pPoolSizes = pool_sizes,
        };
        VK_CHECK(vkCreateDescriptorPool(
            s_ctx.device,
            &pool_info,
            nullptr,
            &s_ctx.bindless_textures_descriptor_pool
        ));

        const VkDescriptorSetAllocateInfo allocate_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = s_ctx.bindless_textures_descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &s_ctx.bindless_textures_descriptor_set_layout,
        };

        VK_CHECK(vkAllocateDescriptorSets(
            s_ctx.device,
            &allocate_info,
            &s_ctx.bindless_textures_descriptor_set
        ));
    }
}

void rhi::destroy() {
    rhi::device_wait_idle();

    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(s_ctx.device, s_ctx.frames[i].image_acquire_semaphore, nullptr);
    }
    vkDestroyDescriptorSetLayout(
        s_ctx.device,
        s_ctx.bindless_textures_descriptor_set_layout,
        nullptr
    );
    vkDestroyDescriptorPool(s_ctx.device, s_ctx.bindless_textures_descriptor_pool, nullptr);

    vmaDestroyAllocator(s_ctx.vma_allocator);
    vkDestroyDevice(s_ctx.device, nullptr);
    vkDestroySurfaceKHR(s_ctx.instance, s_ctx.surface, nullptr);
    vkDestroyInstance(s_ctx.instance, nullptr);
    volkFinalize();
}

const rhi::DeviceProperties& rhi::get_device_properties() {
    return s_ctx.device_properties;
}

rhi::Buffer rhi::create_buffer(const BufferDesc&& desc) {
    DEBUG_ASSERT(desc.size > 0);

    BufferImpl buffer{};

    const VkBufferUsageFlags usage = desc.type == rhi::MEMORY_TYPE_DEFAULT_UNIFORM
        ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    const u32 queue_family_indices[] = {
        s_ctx.graphics_queue_family_idx,
        s_ctx.compute_queue_family_idx,
        s_ctx.transfer_queue_family_idx,
    };

    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = u32(ARRAY_SIZE(queue_family_indices)),
        .pQueueFamilyIndices = queue_family_indices,
    };

    const VmaAllocationCreateInfo allocation_info = {
        .requiredFlags = memory_type_to_vk(desc.type),
    };

    if (desc.min_alignment > 0) {
        VK_CHECK(vmaCreateBufferWithAlignment(
            s_ctx.vma_allocator,
            &buffer_info,
            &allocation_info,
            desc.min_alignment,
            &buffer.buffer,
            &buffer.allocation,
            nullptr
        ));
    } else {
        VK_CHECK(vmaCreateBuffer(
            s_ctx.vma_allocator,
            &buffer_info,
            &allocation_info,
            &buffer.buffer,
            &buffer.allocation,
            nullptr
        ));
    }

    if (allocation_info.requiredFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        VK_CHECK(vmaMapMemory(s_ctx.vma_allocator, buffer.allocation, &buffer.mapped));
    }

    const VkBufferDeviceAddressInfo address_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer.buffer,
    };
    buffer.device_address = vkGetBufferDeviceAddress(s_ctx.device, &address_info);

    if (desc.debug_name) {
        debug_name_object(
            s_ctx.device,
            VK_OBJECT_TYPE_BUFFER,
            reinterpret_cast<u64>(buffer.buffer),
            desc.debug_name
        );
    }

    return s_ctx.buffers.create_handle(buffer);
}

void* rhi::get_buffer_host_ptr(Buffer handle) {
    DEBUG_ASSERT(handle);

    return s_ctx.buffers.get_ptr(handle)->mapped;
}

u64 rhi::get_buffer_device_ptr(Buffer handle) {
    DEBUG_ASSERT(handle);

    return static_cast<u64>(s_ctx.buffers.get_ptr(handle)->device_address);
}

void rhi::unmap_buffer(Buffer handle) {
    DEBUG_ASSERT(handle); // TODO: Remove these kind of asserts.

    BufferImpl* const buffer = s_ctx.buffers.get_ptr(handle);

    if (buffer->mapped) {
        vmaUnmapMemory(s_ctx.vma_allocator, buffer->allocation);
        buffer->mapped = nullptr;
    }
}

void rhi::destroy_buffer(Buffer handle) {
    if (!handle) {
        return;
    }

    BufferImpl* const buffer = s_ctx.buffers.get_ptr(handle);

    if (buffer->mapped) {
        vmaUnmapMemory(s_ctx.vma_allocator, buffer->allocation);
        buffer->mapped = nullptr;
    }
    vmaDestroyBuffer(s_ctx.vma_allocator, buffer->buffer, buffer->allocation);
    buffer->buffer = VK_NULL_HANDLE;
    buffer->allocation = VK_NULL_HANDLE;

    s_ctx.buffers.destroy_handle(handle);
}

rhi::Texture rhi::create_texture(const TextureDesc&& desc) {
    DEBUG_ASSERT(desc.dimensions.x > 0);
    DEBUG_ASSERT(desc.dimensions.y > 0);
    DEBUG_ASSERT(desc.dimensions.z > 0);
    DEBUG_ASSERT(desc.mip_count > 0);
    DEBUG_ASSERT(desc.layer_count > 0);
    DEBUG_ASSERT(desc.format != rhi::FORMAT_UNDEFINED);

    TextureImpl texture{};

    const VkImageType type = texture_type_to_vk(desc.type);
    const VkImageViewType view_type = texture_type_to_view_vk(desc.type);
    const VkImageUsageFlags usage = texture_usage_to_vk(desc.usage);
    const VkFormat format = format_to_vk(desc.format);

    // NOTE: won't check format availability since vkCreateImage should fail in this case.

    const u32 queue_family_indices[] = {
        s_ctx.graphics_queue_family_idx,
        s_ctx.compute_queue_family_idx,
        s_ctx.transfer_queue_family_idx,
    };

    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = type,
        .format = format,
        .extent =
            {
                .width = desc.dimensions.x,
                .height = desc.dimensions.y,
                .depth = desc.dimensions.z,
            },
        .mipLevels = desc.mip_count,
        .arrayLayers = desc.layer_count,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = u32(ARRAY_SIZE(queue_family_indices)),
        .pQueueFamilyIndices = queue_family_indices,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    const VmaAllocationCreateInfo allocation_info = {
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VK_CHECK(vmaCreateImage(
        s_ctx.vma_allocator,
        &image_info,
        &allocation_info,
        &texture.image,
        &texture.allocation,
        nullptr
    ));

    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.image,
        .viewType = view_type,
        .format = format,
        .subresourceRange = {
            .aspectMask = vk_aspect_flags_from_format(format),
            .levelCount = desc.mip_count,
            .layerCount = desc.layer_count,
        },
    };
    VK_CHECK(vkCreateImageView(s_ctx.device, &view_info, nullptr, &texture.view));

    if (desc.debug_name) {
        debug_name_object(
            s_ctx.device,
            VK_OBJECT_TYPE_IMAGE,
            reinterpret_cast<u64>(texture.image),
            desc.debug_name
        );
        debug_name_object(
            s_ctx.device,
            VK_OBJECT_TYPE_IMAGE_VIEW,
            reinterpret_cast<u64>(texture.view),
            desc.debug_name
        );
    }

    texture.type = desc.type;
    texture.format = desc.format;
    texture.dimensions = desc.dimensions;

    return s_ctx.textures.create_handle(texture);
}

void rhi::destroy_texture(rhi::Texture handle) {
    if (!handle) {
        return;
    }

    TextureImpl* const texture = s_ctx.textures.get_ptr(handle);

    vkDestroyImageView(s_ctx.device, texture->view, nullptr);
    texture->view = VK_NULL_HANDLE;
    vmaDestroyImage(s_ctx.vma_allocator, texture->image, texture->allocation);
    texture->allocation = VK_NULL_HANDLE;
    texture->image = VK_NULL_HANDLE;

    s_ctx.textures.destroy_handle(handle);
}

rhi::TextureDescriptor rhi::create_texture_descriptor(const TextureDescriptorDesc&& desc) {
    DEBUG_ASSERT(desc.texture);

    const TextureImpl* const texture = s_ctx.textures.get_ptr(desc.texture);

    TextureViewImpl view{};

    const VkFormat format = format_to_vk(texture->format);

    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->image,
        .viewType = texture_type_to_view_vk(desc.type),
        .format = format,
        .subresourceRange = {
            .aspectMask = vk_aspect_flags_from_format(format),
            .baseMipLevel = desc.base_mip,
            .levelCount = desc.mip_count,
            .baseArrayLayer = desc.base_layer,
            .layerCount = desc.layer_count,
        },
    };
    VK_CHECK(vkCreateImageView(s_ctx.device, &view_info, nullptr, &view.view));

    return s_ctx.texture_descriptors.create_handle(view);
}

void rhi::destroy_texture_descriptor(rhi::TextureDescriptor handle) {
    if (!handle) {
        return;
    }

    TextureViewImpl* const view = s_ctx.texture_descriptors.get_ptr(handle);

    vkDestroyImageView(s_ctx.device, view->view, nullptr);

    s_ctx.texture_descriptors.destroy_handle(handle);
}

rhi::Format rhi::get_texture_format(Texture handle) {
    return s_ctx.textures.get_ptr(handle)->format;
}

Vec3U32 rhi::get_texture_dimensions(Texture handle) {
    return s_ctx.textures.get_ptr(handle)->dimensions;
}

void rhi::update_texture_descriptor_set(rhi::Texture handle, u32 dst_array_element) {
    DEBUG_ASSERT(handle);

    const VkDescriptorImageInfo image_info = {
        .imageView = s_ctx.textures.get_ptr(handle)->view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    const VkWriteDescriptorSet write_set = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = s_ctx.bindless_textures_descriptor_set,
        .dstBinding = 0,
        .dstArrayElement = dst_array_element,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image_info,
    };

    vkUpdateDescriptorSets(s_ctx.device, 1, &write_set, 0, nullptr);
}

rhi::Sampler rhi::create_sampler(const rhi::SamplerDesc&& desc) {
    const VkSamplerReductionModeCreateInfo reductionModeInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
        .reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN,
    };

    const VkSamplerCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = desc.reduction_mode != rhi::SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE
            ? &reductionModeInfo
            : nullptr,
        .magFilter = filter_to_vk(desc.mag_filter),
        .minFilter = filter_to_vk(desc.mag_filter),
        .mipmapMode = sampler_mipmap_mode_to_vk(desc.mipmap_mode),
        .addressModeU = sampler_address_mode_to_vk(desc.address_mode_u),
        .addressModeV = sampler_address_mode_to_vk(desc.address_mode_v),
        .addressModeW = sampler_address_mode_to_vk(desc.address_mode_w),
        .anisotropyEnable = desc.anisotropy_enable,
        .maxAnisotropy = desc.max_anisotropy,
        .compareEnable = desc.compare_enable,
        .compareOp = compare_op_to_vk(desc.compare_op),
        .minLod = desc.min_lod,
        .maxLod = desc.max_lod,
    };

    VkSampler sampler{};

    VK_CHECK(vkCreateSampler(s_ctx.device, &create_info, nullptr, &sampler));

    return s_ctx.samplers.create_handle(sampler);
}

void rhi::destroy_sampler(rhi::Sampler handle) {
    if (!handle) {
        return;
    }

    vkDestroySampler(s_ctx.device, *s_ctx.samplers.get_ptr(handle), nullptr);

    s_ctx.samplers.destroy_handle(handle);
}

rhi::Semaphore rhi::create_semaphore(u64 initial_value) {
    const VkSemaphoreTypeCreateInfo timelineSemaphoreTypeInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = initial_value,
    };
    const VkSemaphoreCreateInfo timeline_semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timelineSemaphoreTypeInfo,
    };

    VkSemaphore semaphore{};

    VK_CHECK(vkCreateSemaphore(s_ctx.device, &timeline_semaphore_info, nullptr, &semaphore));

    return s_ctx.semaphores.create_handle(semaphore);
}

void rhi::destroy_semaphore(rhi::Semaphore handle) {
    if (!handle) {
        return;
    }

    vkDestroySemaphore(s_ctx.device, *s_ctx.semaphores.get_ptr(handle), nullptr);

    s_ctx.semaphores.destroy_handle(handle);
}

u64 rhi::get_semaphore_value(rhi::Semaphore handle) {
    DEBUG_ASSERT(handle);

    u64 value = 0;
    VK_CHECK(vkGetSemaphoreCounterValue(s_ctx.device, *s_ctx.semaphores.get_ptr(handle), &value));
    return value;
}

void rhi::wait_semaphore(rhi::Semaphore handle, u64 value, u64 timeout) {
    DEBUG_ASSERT(handle);

    const VkSemaphoreWaitInfo wait_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = s_ctx.semaphores.get_ptr(handle),
        .pValues = &value,
    };

    VK_CHECK(vkWaitSemaphores(s_ctx.device, &wait_info, timeout));
}

rhi::CommandPool rhi::create_command_pool(const CommandPoolDesc&& desc) {
    const QueueImpl& queue = *s_ctx.queues.get_ptr(desc.queue);

    u32 family_idx = UINT32_MAX;
    switch (queue.type) {
        case rhi::QUEUE_TYPE_GRAPHICS:
            family_idx = s_ctx.graphics_queue_family_idx;
            break;
        case rhi::QUEUE_TYPE_COMPUTE:
            family_idx = s_ctx.compute_queue_family_idx;
            break;
        case rhi::QUEUE_TYPE_TRANSFER:
            family_idx = s_ctx.transfer_queue_family_idx;
            break;
    }
    ASSERT(family_idx != UINT32_MAX);

    const VkCommandPoolCreateInfo cmd_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags =
            desc.transient ? VK_COMMAND_POOL_CREATE_TRANSIENT_BIT : VkCommandPoolCreateFlags{0},
        .queueFamilyIndex = family_idx,
    };

    VkCommandPool pool{};
    VK_CHECK(vkCreateCommandPool(s_ctx.device, &cmd_pool_info, nullptr, &pool));

    return s_ctx.command_pools.create_handle(pool);
}

void rhi::destroy_command_pool(CommandPool handle) {
    if (handle) {
        vkDestroyCommandPool(s_ctx.device, *s_ctx.command_pools.get_ptr(handle), nullptr);
        s_ctx.command_pools.destroy_handle(handle);
    }
}

void rhi::reset_command_pool(CommandPool handle) {
    VK_CHECK(vkResetCommandPool(s_ctx.device, *s_ctx.command_pools.get_ptr(handle), 0));
}

rhi::CommandBuffer rhi::create_command_buffer(const CommandBufferDesc&& desc) {
    const VkCommandPool pool = *s_ctx.command_pools.get_ptr(desc.command_pool);

    const VkCommandBufferAllocateInfo cmd_buffer_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    CommandBufferImpl cb{};
    VK_CHECK(vkAllocateCommandBuffers(s_ctx.device, &cmd_buffer_allocate_info, &cb.command_buffer));

    cb.command_pool = pool;

    if (desc.debug_name) {
        debug_name_object(
            s_ctx.device,
            VK_OBJECT_TYPE_COMMAND_BUFFER,
            reinterpret_cast<u64>(cb.command_buffer),
            desc.debug_name
        );
    }

    return s_ctx.command_buffers.create_handle(cb);
}

void rhi::destroy_command_buffer(rhi::CommandBuffer handle) {
    if (handle) {
        const CommandBufferImpl* const cb = s_ctx.command_buffers.get_ptr(handle);
        vkFreeCommandBuffers(s_ctx.device, cb->command_pool, 1, &cb->command_buffer);
        s_ctx.command_buffers.destroy_handle(handle);
    }
}

void rhi::begin_command_buffer(rhi::CommandBuffer handle) {
    DEBUG_ASSERT(handle);

    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(
        vkBeginCommandBuffer(s_ctx.command_buffers.get_ptr(handle)->command_buffer, &begin_info)
    );
}

void rhi::end_command_buffer(rhi::CommandBuffer handle) {
    DEBUG_ASSERT(handle);

    VK_CHECK(vkEndCommandBuffer(s_ctx.command_buffers.get_ptr(handle)->command_buffer));
}

rhi::Queue rhi::create_queue(const QueueDesc&& desc) {
    if (desc.type == rhi::QUEUE_TYPE_GRAPHICS) {
        QueueImpl queue = {.type = rhi::QUEUE_TYPE_GRAPHICS};

        const bool found = find_queue_index(queue.idx, s_ctx.graphics_queue_idxs);
        ASSERT(found);

        vkGetDeviceQueue(s_ctx.device, s_ctx.graphics_queue_family_idx, queue.idx, &queue.queue);

        debug_name_object(
            s_ctx.device,
            VK_OBJECT_TYPE_QUEUE,
            reinterpret_cast<u64>(queue.queue),
            desc.debug_name
        );

        return s_ctx.queues.create_handle(queue);
    } else if (desc.type == rhi::QUEUE_TYPE_COMPUTE) {
        QueueImpl queue = {.type = rhi::QUEUE_TYPE_COMPUTE};

        const bool found = find_queue_index(queue.idx, s_ctx.compute_queue_idxs);
        ASSERT(found);

        vkGetDeviceQueue(s_ctx.device, s_ctx.compute_queue_family_idx, queue.idx, &queue.queue);

        debug_name_object(
            s_ctx.device,
            VK_OBJECT_TYPE_QUEUE,
            reinterpret_cast<u64>(queue.queue),
            desc.debug_name
        );

        return s_ctx.queues.create_handle(queue);
    } else if (desc.type == rhi::QUEUE_TYPE_TRANSFER) {
        QueueImpl queue = {.type = rhi::QUEUE_TYPE_TRANSFER};

        const bool found = find_queue_index(queue.idx, s_ctx.transfer_queue_idxs);
        ASSERT(found);

        vkGetDeviceQueue(s_ctx.device, s_ctx.transfer_queue_family_idx, queue.idx, &queue.queue);

        debug_name_object(
            s_ctx.device,
            VK_OBJECT_TYPE_QUEUE,
            reinterpret_cast<u64>(queue.queue),
            desc.debug_name
        );

        return s_ctx.queues.create_handle(queue);
    } else {
        UNREACHABLE();
    }
}

void rhi::destroy_queue(Queue handle) {
    if (handle) {
        const QueueImpl& queue = *s_ctx.queues.get_ptr(handle);

        if (queue.type == rhi::QUEUE_TYPE_GRAPHICS) {
            s_ctx.graphics_queue_idxs[queue.idx] = 0;
            s_ctx.queues.destroy_handle(handle);
        } else if (queue.type == rhi::QUEUE_TYPE_COMPUTE) {
            s_ctx.compute_queue_idxs[queue.idx] = 0;
            s_ctx.queues.destroy_handle(handle);
        } else if (queue.type == rhi::QUEUE_TYPE_TRANSFER) {
            s_ctx.transfer_queue_idxs[queue.idx] = 0;
            s_ctx.queues.destroy_handle(handle);
        } else {
            UNREACHABLE();
        }
    }
}

void rhi::queue_submit(rhi::Queue queue, const SliceArg<QueueSubmitDesc>&& desc) {
    DEBUG_ASSERT(desc.count > 0);

    TEMP_ALLOCATOR(temp);

    TempVector<VkSubmitInfo2> submit_infos{desc.count, temp};

    for (int i = 0; i < desc.count; ++i) {
        const CommandBufferImpl* const cb = s_ctx.command_buffers.get_ptr(desc[i].cb);

        // +1 for optional image acquire binary semaphore.
        TempVector<VkSemaphoreSubmitInfo> sem_wait_submit_infos{
            desc[i].wait_semaphores.count + 1,
            temp
        };

        u64 wait_semaphore_count = desc[i].wait_semaphores.count;
        for (u64 j = 0; j < wait_semaphore_count; ++j) {
            sem_wait_submit_infos[j] = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = *s_ctx.semaphores.get_ptr(desc[i].wait_semaphores[j].semaphore),
                .value = desc[i].wait_semaphores[j].value,
                .stageMask = stage_to_vk(desc[i].wait_semaphores[j].stage_mask),
            };
        };

        if (desc[i].wait_for_texture_acquire) {
            sem_wait_submit_infos[wait_semaphore_count++] = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = s_ctx.frames[s_ctx.frame_idx].image_acquire_semaphore,
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        // +1 for optional present binary semaphore.
        TempVector<VkSemaphoreSubmitInfo> sem_signal_submit_infos{
            desc[i].signal_semaphores.count + 1,
            temp
        };

        u64 signal_semaphore_count = desc[i].signal_semaphores.count;
        for (u64 j = 0; j < desc[i].signal_semaphores.count; ++j) {
            sem_signal_submit_infos[j] = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = *s_ctx.semaphores.get_ptr(desc[i].signal_semaphores[j].semaphore),
                .value = desc[i].signal_semaphores[j].value,
                .stageMask = stage_to_vk(desc[i].signal_semaphores[j].stage_mask),
            };
        };

        if (desc[i].signal_ready_to_present) {
            sem_signal_submit_infos[signal_semaphore_count++] = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = s_ctx.swapchain.ready_to_present_semaphores[s_ctx.image_idx],
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            };
        }

        TempVector<VkCommandBufferSubmitInfo> cb_submit_info{1, temp};
        cb_submit_info[0] = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cb->command_buffer,
        };

        submit_infos[i] = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = u32(wait_semaphore_count),
            .pWaitSemaphoreInfos = sem_wait_submit_infos.data(),
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = cb_submit_info.data(),
            .signalSemaphoreInfoCount = u32(signal_semaphore_count),
            .pSignalSemaphoreInfos = sem_signal_submit_infos.data(),
        };
    }

    VK_CHECK(vkQueueSubmit2(
        s_ctx.queues.get_ptr(queue)->queue,
        u32(desc.count),
        submit_infos.data(),
        VK_NULL_HANDLE
    ));
}

rhi::Pipeline rhi::create_compute_pipeline(const rhi::ComputePipelineDesc&& desc) {
    DEBUG_ASSERT(desc.bytecode.count > 0);

    eastl::vector<VkDescriptorSetLayoutBinding> descriptor_set_layout_bindings;
    descriptor_set_layout_bindings.reserve(32);
    ShaderImpl shader{};
    DEFER(vkDestroyShaderModule(s_ctx.device, shader.module, nullptr));

    if (!create_shader(shader, descriptor_set_layout_bindings, s_ctx.device, desc.bytecode)) {
        return rhi::Pipeline::invalid();
    }

    // Remove duplicates.
    bool binding_used[32]{};
    eastl::vector<VkDescriptorSetLayoutBinding> unique_descriptor_set_layout_bindings(
        descriptor_set_layout_bindings.size()
    );
    size_t unique_binding_count = 0;
    for (const VkDescriptorSetLayoutBinding& b: descriptor_set_layout_bindings) {
        ASSERT(b.binding < ARRAY_SIZE(binding_used));
        if (!binding_used[b.binding]) {
            unique_descriptor_set_layout_bindings[unique_binding_count] = b;
            binding_used[b.binding] = true;
            ++unique_binding_count;
        }
    }
    unique_descriptor_set_layout_bindings.resize(unique_binding_count);

    // Sort for stable indices.
    eastl::sort(
        unique_descriptor_set_layout_bindings.begin(),
        unique_descriptor_set_layout_bindings.end(),
        [](VkDescriptorSetLayoutBinding a, VkDescriptorSetLayoutBinding b) {
            return a.binding < b.binding;
        }
    );

    const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
        .bindingCount = u32(unique_descriptor_set_layout_bindings.size()),
        .pBindings = unique_descriptor_set_layout_bindings.data(),
    };

    bool failed = true;
    PipelineImpl pipeline{};
    // clang-format off
    DEFER(
        if (failed) {
            vkDestroyPipelineLayout(s_ctx.device, pipeline.layout, nullptr);
            vkDestroyDescriptorSetLayout(s_ctx.device, pipeline.descriptor_set_layout, nullptr);
            vkDestroyPipeline(s_ctx.device, pipeline.pipeline, nullptr);
            vkDestroyDescriptorUpdateTemplate(
                s_ctx.device,
                pipeline.descriptor_update_template,
                nullptr
            );
        }
    );
    // clang-format on

    VK_CHECK_HANDLE(vkCreateDescriptorSetLayout(
        s_ctx.device,
        &descriptor_set_layout_info,
        nullptr,
        &pipeline.descriptor_set_layout
    ));

    VkPushConstantRange push_constant_range{};
    const bool uses_push_constants = shader.push_constant_size > 0;
    if (uses_push_constants) {
        push_constant_range.stageFlags = VK_SHADER_STAGE_ALL;
        push_constant_range.size = shader.push_constant_size;
        pipeline.push_constant_size = shader.push_constant_size;
    }

    const VkDescriptorSetLayout set_layouts[] = {
        pipeline.descriptor_set_layout,
        s_ctx.bindless_textures_descriptor_set_layout,
    };

    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc.uses_bindless_textures ? 2u : 1u,
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = uses_push_constants ? 1u : 0u,
        .pPushConstantRanges = &push_constant_range,
    };
    VK_CHECK_HANDLE(vkCreatePipelineLayout(s_ctx.device, &layout_info, nullptr, &pipeline.layout));

    const VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = shader.stage,
                .module = shader.module,
                .pName = "main",
            },
        .layout = pipeline.layout,
    };

    VK_CHECK_HANDLE(vkCreateComputePipelines(
        s_ctx.device,
        VK_NULL_HANDLE,
        1,
        &pipeline_info,
        nullptr,
        &pipeline.pipeline
    ));

    eastl::vector<VkDescriptorUpdateTemplateEntry> descriptor_update_template_entries(
        unique_descriptor_set_layout_bindings.size()
    );

    for (size_t i = 0; i < unique_descriptor_set_layout_bindings.size(); ++i) {
        const VkDescriptorUpdateTemplateEntry entry = {
            .dstBinding = unique_descriptor_set_layout_bindings[i].binding,
            .descriptorCount = 1,
            .descriptorType = unique_descriptor_set_layout_bindings[i].descriptorType,
            .offset = sizeof(DescriptorInfoImpl) * i,
            .stride = sizeof(DescriptorInfoImpl),
        };

        descriptor_update_template_entries[i] = entry;
    }

    const VkDescriptorUpdateTemplateCreateInfo descriptor_update_template_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .descriptorUpdateEntryCount = u32(descriptor_update_template_entries.size()),
        .pDescriptorUpdateEntries = descriptor_update_template_entries.data(),
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE,
        .pipelineLayout = pipeline.layout,
    };

    if (!descriptor_update_template_entries.empty()) {
        VK_CHECK_HANDLE(vkCreateDescriptorUpdateTemplate(
            s_ctx.device,
            &descriptor_update_template_info,
            nullptr,
            &pipeline.descriptor_update_template
        ));
    }

    if (desc.debug_name) {
        debug_name_object(
            s_ctx.device,
            VK_OBJECT_TYPE_PIPELINE,
            reinterpret_cast<u64>(pipeline.pipeline),
            desc.debug_name
        );
    }

    failed = false;

    pipeline.local_size = shader.local_size;

    return s_ctx.pipelines.create_handle(pipeline);
}

rhi::Pipeline rhi::create_graphics_pipeline(const rhi::GraphicsPipelineDesc&& desc) {
    DEBUG_ASSERT(desc.bytecodes.count > 0);

    eastl::vector<VkDescriptorSetLayoutBinding> descriptor_set_layout_bindings;
    descriptor_set_layout_bindings.reserve(32);
    eastl::vector<ShaderImpl> shaders(size_t(desc.bytecodes.count));

    bool failed = true;
    PipelineImpl pipeline{};

    // clang-format off
    DEFER(
        for (ShaderImpl& shader : shaders) {
            vkDestroyShaderModule(s_ctx.device, shader.module, nullptr);
        }
        if (failed) {
            vkDestroyPipelineLayout(s_ctx.device, pipeline.layout, nullptr);
            vkDestroyDescriptorSetLayout(s_ctx.device, pipeline.descriptor_set_layout, nullptr);
            vkDestroyPipeline(s_ctx.device, pipeline.pipeline, nullptr);
            vkDestroyDescriptorUpdateTemplate(
                s_ctx.device,
                pipeline.descriptor_update_template,
                nullptr
            );
        }
    );
    // clang-format on

    for (int i = 0; i < desc.bytecodes.count; ++i) {
        if (!create_shader(
                shaders[size_t(i)],
                descriptor_set_layout_bindings,
                s_ctx.device,
                *(desc.bytecodes.begin() + i)
            ))
        {
            return rhi::Pipeline::invalid();
        }
    }

    u32 push_constant_size = 0;
    for (ShaderImpl& shader: shaders) {
        ASSERT(
            (push_constant_size == 0 || shader.push_constant_size == 0)
            || (push_constant_size == shader.push_constant_size)
        );
        push_constant_size =
            push_constant_size == 0 ? shader.push_constant_size : push_constant_size;
    }

    // Remove duplicates.
    bool binding_used[32]{};
    eastl::vector<VkDescriptorSetLayoutBinding> unique_descriptor_set_layout_bindings(
        descriptor_set_layout_bindings.size()
    );
    size_t unique_binding_count = 0;
    for (const VkDescriptorSetLayoutBinding& b: descriptor_set_layout_bindings) {
        ASSERT(b.binding < ARRAY_SIZE(binding_used));
        if (!binding_used[b.binding]) {
            unique_descriptor_set_layout_bindings[unique_binding_count] = b;
            binding_used[b.binding] = true;
            ++unique_binding_count;
        }
    }
    unique_descriptor_set_layout_bindings.resize(unique_binding_count);

    // Sort for stable indices.
    eastl::sort(
        unique_descriptor_set_layout_bindings.begin(),
        unique_descriptor_set_layout_bindings.end(),
        [](VkDescriptorSetLayoutBinding a, VkDescriptorSetLayoutBinding b) {
            return a.binding < b.binding;
        }
    );

    const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
        .bindingCount = u32(unique_descriptor_set_layout_bindings.size()),
        .pBindings = unique_descriptor_set_layout_bindings.data(),
    };

    VK_CHECK(vkCreateDescriptorSetLayout(
        s_ctx.device,
        &descriptor_set_layout_info,
        nullptr,
        &pipeline.descriptor_set_layout
    ));

    VkPushConstantRange push_constant_range{};
    const bool uses_push_constants = push_constant_size > 0;
    if (uses_push_constants) {
        push_constant_range.stageFlags = VK_SHADER_STAGE_ALL;
        push_constant_range.size = push_constant_size;
        pipeline.push_constant_size = push_constant_size;
    }

    const VkDescriptorSetLayout set_layouts[] = {
        pipeline.descriptor_set_layout,
        s_ctx.bindless_textures_descriptor_set_layout,
    };

    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc.uses_bindless_textures ? 2u : 1u,
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = uses_push_constants ? 1u : 0u,
        .pPushConstantRanges = &push_constant_range,
    };
    VK_CHECK_HANDLE(vkCreatePipelineLayout(s_ctx.device, &layout_info, nullptr, &pipeline.layout));

    eastl::vector<VkPipelineShaderStageCreateInfo> shader_stage_infos(shaders.size());
    for (size_t i = 0; i < shaders.size(); ++i) {
        shader_stage_infos[i] = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = shaders[i].stage,
            .module = shaders[i].module,
            .pName = "main",
        };
    }

    // TODO: Well, I have to choose if I care about the performance for this
    // function, if not then eastl::vector is fine.
    TEMP_ALLOCATOR(temp);

    TempVector<VkFormat> color_attachment_formats{desc.color_targets.count, temp};
    for (int i = 0; i < desc.color_targets.count; ++i) {
        color_attachment_formats[i] = format_to_vk(desc.color_targets[i].format);
    }

    const VkPipelineRenderingCreateInfo pipeline_rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = u32(desc.color_targets.count),
        .pColorAttachmentFormats = color_attachment_formats.data(),
        .depthAttachmentFormat = format_to_vk(desc.depth_format),
        .stencilAttachmentFormat = format_to_vk(desc.stencil_format),
    };

    const VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    const VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = primitive_topology_to_vk(desc.topology),
    };

    const VkPipelineViewportStateCreateInfo viewport_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const VkPipelineRasterizationStateCreateInfo rasterization_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = desc.depth_clamp_enable,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = cull_to_vk(desc.cull),
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    const VkPipelineMultisampleStateCreateInfo multisample_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    const VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc.depth_mask & rhi::DEPTH_READ_BIT,
        .depthWriteEnable = desc.depth_mask & rhi::DEPTH_WRITE_BIT,
        .depthCompareOp = VK_COMPARE_OP_GREATER,
    };

    TempVector<VkPipelineColorBlendAttachmentState> color_blend_attachments{
        desc.color_targets.count,
        temp
    };

    for (int i = 0; i < desc.color_targets.count; ++i) {
        const rhi::PipelineColorTarget& t = desc.color_targets[i];
        color_blend_attachments[i] = {
            .blendEnable = t.blend_enable,
            .srcColorBlendFactor = blend_factor_to_vk(t.src_color_factor),
            .dstColorBlendFactor = blend_factor_to_vk(t.dst_color_factor),
            .colorBlendOp = blend_op_to_vk(t.color_op),
            .srcAlphaBlendFactor = blend_factor_to_vk(t.dst_alpha_factor),
            .dstAlphaBlendFactor = blend_factor_to_vk(t.dst_alpha_factor),
            .alphaBlendOp = blend_op_to_vk(t.alpha_op),
            .colorWriteMask = color_component_to_vk(t.color_component_mask),
        };
    }

    const VkPipelineColorBlendStateCreateInfo color_blending_info = {
        .attachmentCount = u32(desc.color_targets.count),
        .pAttachments = color_blend_attachments.data(),
    };

    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    const VkPipelineDynamicStateCreateInfo dynamic_state_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = u32(ARRAY_SIZE(dynamic_states)),
        .pDynamicStates = dynamic_states
    };

    const VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipeline_rendering_info,
        .stageCount = u32(shader_stage_infos.size()),
        .pStages = shader_stage_infos.data(),
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly_info,
        .pViewportState = &viewport_info,
        .pRasterizationState = &rasterization_info,
        .pMultisampleState = &multisample_info,
        .pDepthStencilState = &depth_stencil_info,
        .pColorBlendState = &color_blending_info,
        .pDynamicState = &dynamic_state_info,
        .layout = pipeline.layout,
    };

    VK_CHECK_HANDLE(vkCreateGraphicsPipelines(
        s_ctx.device,
        VK_NULL_HANDLE,
        1,
        &pipeline_info,
        nullptr,
        &pipeline.pipeline
    ));

    eastl::vector<VkDescriptorUpdateTemplateEntry> descriptor_update_template_entries(
        unique_descriptor_set_layout_bindings.size()
    );

    for (size_t i = 0; i < unique_descriptor_set_layout_bindings.size(); ++i) {
        const VkDescriptorUpdateTemplateEntry entry = {
            .dstBinding = unique_descriptor_set_layout_bindings[i].binding,
            .descriptorCount = 1,
            .descriptorType = unique_descriptor_set_layout_bindings[i].descriptorType,
            .offset = sizeof(DescriptorInfoImpl) * i,
            .stride = sizeof(DescriptorInfoImpl),
        };

        descriptor_update_template_entries[i] = entry;
    }

    const VkDescriptorUpdateTemplateCreateInfo descriptor_update_template_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .descriptorUpdateEntryCount = u32(descriptor_update_template_entries.size()),
        .pDescriptorUpdateEntries = descriptor_update_template_entries.data(),
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pipelineLayout = pipeline.layout,
    };

    if (!descriptor_update_template_entries.empty()) {
        VK_CHECK_HANDLE(vkCreateDescriptorUpdateTemplate(
            s_ctx.device,
            &descriptor_update_template_info,
            nullptr,
            &pipeline.descriptor_update_template
        ));
    }

    if (desc.debug_name) {
        debug_name_object(
            s_ctx.device,
            VK_OBJECT_TYPE_PIPELINE,
            reinterpret_cast<u64>(pipeline.pipeline),
            desc.debug_name
        );
    }

    failed = false;

    return s_ctx.pipelines.create_handle(pipeline);
}

Vec3U32 rhi::get_pipeline_local_size(rhi::Pipeline handle) {
    DEBUG_ASSERT(handle);

    return s_ctx.pipelines.get_ptr(handle)->local_size;
}

void rhi::destroy_pipeline(rhi::Pipeline handle) {
    if (!handle) {
        return;
    }

    const PipelineImpl* const pipeline = s_ctx.pipelines.get_ptr(handle);

    vkDestroyPipelineLayout(s_ctx.device, pipeline->layout, nullptr);
    vkDestroyDescriptorSetLayout(s_ctx.device, pipeline->descriptor_set_layout, nullptr);
    vkDestroyPipeline(s_ctx.device, pipeline->pipeline, nullptr);
    vkDestroyDescriptorUpdateTemplate(s_ctx.device, pipeline->descriptor_update_template, nullptr);

    s_ctx.pipelines.destroy_handle(handle);
}

void rhi::cmd_barrier(
    rhi::CommandBuffer cb,
    rhi::StageFlags src_stage_mask,
    rhi::AccessFlags src_access_mask,
    rhi::StageFlags dst_stage_mask,
    rhi::AccessFlags dst_access_mask
) {
    DEBUG_ASSERT(cb);

    const VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = stage_to_vk(src_stage_mask),
        .srcAccessMask = access_to_vk(src_access_mask),
        .dstStageMask = stage_to_vk(dst_stage_mask),
        .dstAccessMask = access_to_vk(dst_access_mask),
    };

    const VkDependencyInfo dependency_info = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(s_ctx.command_buffers.get_ptr(cb)->command_buffer, &dependency_info);
}

void rhi::cmd_texture_barrier(
    rhi::CommandBuffer handle,
    const SliceArg<rhi::TextureBarrierDesc>&& desc
) {
    DEBUG_ASSERT(handle);
    DEBUG_ASSERT(desc.count > 0);

    TEMP_ALLOCATOR(temp);

    TempVector<VkImageMemoryBarrier2> barriers{desc.count, temp};

    for (int i = 0; i < desc.count; ++i) {
        const rhi::TextureBarrierDesc& d = desc[i];

        const TextureImpl* const texture = s_ctx.textures.get_ptr(d.texture);

        barriers[i] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = stage_to_vk(d.src_stage_mask),
            .srcAccessMask = access_to_vk(d.src_access_mask),
            .dstStageMask = stage_to_vk(d.dst_stage_mask),
            .dstAccessMask = access_to_vk(d.dst_access_mask),
            .oldLayout = texture_layout_to_vk(d.old_layout),
            .newLayout = texture_layout_to_vk(d.new_layout),
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture->image,
            .subresourceRange = {
                .aspectMask = vk_aspect_flags_from_format(format_to_vk(texture->format)),
                .baseMipLevel = 0,
                .levelCount = d.mip_count,
                .baseArrayLayer = 0,
                .layerCount = d.layer_count,
            },
        };
    }

    const CommandBufferImpl* const cb = s_ctx.command_buffers.get_ptr(handle);

    const VkDependencyInfo dependency_info = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = u32(desc.count),
        .pImageMemoryBarriers = barriers.data(),
    };
    vkCmdPipelineBarrier2(cb->command_buffer, &dependency_info);
}

void rhi::cmd_texture_invalidate_barrier(
    CommandBuffer cb,
    StageFlags src_stage_mask,
    AccessFlags src_access_mask,
    StageFlags dst_stage_mask,
    AccessFlags dst_access_mask,
    const SliceArg<Texture>&& textures
) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(textures.count > 0);

    TEMP_ALLOCATOR(temp);

    TempVector<VkImageMemoryBarrier2> barriers{textures.count, temp};

    for (int i = 0; i < textures.count; ++i) {
        const TextureImpl* const texture = s_ctx.textures.get_ptr(textures[i]);

        barriers[i] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = stage_to_vk(src_stage_mask),
            .srcAccessMask = access_to_vk(src_access_mask),
            .dstStageMask = stage_to_vk(dst_stage_mask),
            .dstAccessMask = access_to_vk(dst_access_mask),
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture->image,
            .subresourceRange = {
                .aspectMask = vk_aspect_flags_from_format(format_to_vk(texture->format)),
                .baseMipLevel = 0,
                .levelCount = rhi::ALL_MIPS,
                .baseArrayLayer = 0,
                .layerCount = rhi::ALL_LAYERS,
            },
        };
    }

    const VkDependencyInfo dependency_info = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = u32(textures.count),
        .pImageMemoryBarriers = barriers.data(),
    };

    vkCmdPipelineBarrier2(s_ctx.command_buffers.get_ptr(cb)->command_buffer, &dependency_info);
}

void rhi::cmd_copy_buffer_to_texture(
    rhi::CommandBuffer cb,
    rhi::Buffer buffer,
    rhi::Texture texture,
    const SliceArg<rhi::BufferTextureCopy>&& copy_regions
) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(buffer);
    DEBUG_ASSERT(texture);
    DEBUG_ASSERT(copy_regions.count > 0);

    TEMP_ALLOCATOR(temp);

    TempVector<VkBufferImageCopy> regions{copy_regions.count, temp};

    const TextureImpl* const tex = s_ctx.textures.get_ptr(texture);

    for (int i = 0; i < copy_regions.count; ++i) {
        const rhi::BufferTextureCopy& c = copy_regions[i];
        regions[i] = VkBufferImageCopy{
            .bufferOffset = c.buffer_offset,
            .bufferRowLength = c.buffer_row_length,
            .bufferImageHeight = c.buffer_texture_height,
            .imageSubresource{
                .aspectMask = vk_aspect_flags_from_format(format_to_vk(tex->format)),
                .mipLevel = c.texture_subresource.mip_level,
                .baseArrayLayer = c.texture_subresource.base_array_layer,
                .layerCount = c.texture_subresource.layer_count,
            },
            .imageOffset = {c.texture_offset.x, c.texture_offset.y, c.texture_offset.z},
            .imageExtent = {c.texture_dimensions.x, c.texture_dimensions.y, c.texture_dimensions.z},
        };
    }

    vkCmdCopyBufferToImage(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        s_ctx.buffers.get_ptr(buffer)->buffer,
        tex->image,
        VK_IMAGE_LAYOUT_GENERAL,
        u32(copy_regions.count),
        regions.data()
    );
}

void rhi::cmd_fill_buffer(
    rhi::CommandBuffer cb,
    rhi::Buffer buffer,
    u64 offset,
    u64 size,
    u32 data
) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(buffer);
    DEBUG_ASSERT(size > 0);

    vkCmdFillBuffer(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        s_ctx.buffers.get_ptr(buffer)->buffer,
        offset,
        size,
        data
    );
}

void rhi::cmd_bind_pipeline(rhi::CommandBuffer cb, rhi::Pipeline pipeline) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(pipeline);

    const PipelineImpl* const vk_pipeline = s_ctx.pipelines.get_ptr(pipeline);

    vkCmdBindPipeline(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        get_pipeline_bind_point(*vk_pipeline),
        vk_pipeline->pipeline
    );
}

void rhi::cmd_dispatch(rhi::CommandBuffer cb, Vec3U32 group_count) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(group_count.x > 0);
    DEBUG_ASSERT(group_count.y > 0);
    DEBUG_ASSERT(group_count.z > 0);

    vkCmdDispatch(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        group_count.x,
        group_count.y,
        group_count.z
    );
}

void rhi::cmd_push_descriptors(
    rhi::CommandBuffer cb,
    rhi::Pipeline pipeline,
    const SliceArg<rhi::DescriptorInfo>&& descriptors
) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(pipeline);

    TEMP_ALLOCATOR(temp);

    TempVector<DescriptorInfoImpl> infos{descriptors.count, temp};

    for (int i = 0; i < descriptors.count; ++i) {
        const rhi::DescriptorInfo& d = descriptors[i];
        switch (d.type) {
            case rhi::DescriptorInfo::TYPE_NONE:
                ASSERT(0);
                break;
            case rhi::DescriptorInfo::TYPE_TEXTURE:
                infos[i] = {s_ctx.textures.get_ptr(d.resource.texture)->view};
                break;
            case rhi::DescriptorInfo::TYPE_TEXTURE_DESCRIPTOR:
                infos[i] = {s_ctx.texture_descriptors.get_ptr(d.resource.texture_descriptor)->view};
                break;
            case rhi::DescriptorInfo::TYPE_SAMPLER:
                infos[i] = {*s_ctx.samplers.get_ptr(d.resource.sampler)};
                break;
            case rhi::DescriptorInfo::TYPE_BUFFER: {
                const auto& info = d.resource.buffer;
                infos[i] = {s_ctx.buffers.get_ptr(info.buffer)->buffer, info.offset, info.range};
                break;
            }
        }
    }

    const PipelineImpl* const vk_pipeline = s_ctx.pipelines.get_ptr(pipeline);

    vkCmdPushDescriptorSetWithTemplate(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        vk_pipeline->descriptor_update_template,
        vk_pipeline->layout,
        0,
        infos.data()
    );
}

void rhi::cmd_push_constants(rhi::CommandBuffer cb, rhi::Pipeline pipeline, const void* data) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(pipeline);
    DEBUG_ASSERT(data);

    const PipelineImpl* const vk_pipeline = s_ctx.pipelines.get_ptr(pipeline);

    vkCmdPushConstants(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        vk_pipeline->layout,
        VK_SHADER_STAGE_ALL,
        0,
        vk_pipeline->push_constant_size,
        data
    );
}

void rhi::cmd_set_viewport(const rhi::SetViewportDesc&& desc) {
    DEBUG_ASSERT(desc.cb);
    DEBUG_ASSERT(desc.width >= 0.0f);
    DEBUG_ASSERT(desc.min_depth >= 0.0f);
    DEBUG_ASSERT(desc.max_depth >= 0.0f);

    const VkViewport viewport = {
        .x = desc.x,
        .y = desc.y,
        .width = desc.width,
        .height = desc.height,
        .minDepth = desc.min_depth,
        .maxDepth = desc.max_depth,
    };

    vkCmdSetViewport(s_ctx.command_buffers.get_ptr(desc.cb)->command_buffer, 0, 1, &viewport);
}

void rhi::cmd_set_scissor(const rhi::SetScissorDesc&& desc) {
    DEBUG_ASSERT(desc.cb);

    const VkRect2D scissor = {
        .offset = {desc.offset.x, desc.offset.y},
        .extent = {desc.extent.x, desc.extent.y},
    };

    vkCmdSetScissor(s_ctx.command_buffers.get_ptr(desc.cb)->command_buffer, 0, 1, &scissor);
}

void rhi::cmd_begin_rendering(const BeginRenderingDesc&& desc) {
    DEBUG_ASSERT(desc.cb);

    TEMP_ALLOCATOR(temp);

    TempVector<VkRenderingAttachmentInfo> rendering_attachment_infos{
        desc.color_targets.count,
        temp
    };

    for (int i = 0; i < desc.color_targets.count; ++i) {
        const rhi::Attachment& t = desc.color_targets[i];

        rendering_attachment_infos[i] = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = t.attachment.type == rhi::Attachment::TextureOrDescriptor::TYPE_TEXTURE
                ? s_ctx.textures.get_ptr(t.attachment.texture)->view
                : s_ctx.texture_descriptors.get_ptr(t.attachment.descriptor)->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = attachment_load_op_to_vk(t.load_op),
            .storeOp = attachment_store_op_to_vk(t.store_op),
        };
    }

    const bool depth_attachment_exists =
        desc.depth_target.attachment.type != rhi::Attachment::TextureOrDescriptor::TYPE_NONE;

    VkRenderingAttachmentInfo depth_attachment_info{};

    if (depth_attachment_exists) {
        depth_attachment_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = desc.depth_target.attachment.type
                    == rhi::Attachment::TextureOrDescriptor::TYPE_TEXTURE
                ? s_ctx.textures.get_ptr(desc.depth_target.attachment.texture)->view
                : s_ctx.texture_descriptors.get_ptr(desc.depth_target.attachment.descriptor)->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = attachment_load_op_to_vk(desc.depth_target.load_op),
            .storeOp = attachment_store_op_to_vk(desc.depth_target.store_op),
        };
    }

    const VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{
            .offset = {desc.offset.x, desc.offset.y},
            .extent = {desc.extent.x, desc.extent.y},
        },
        .layerCount = 1,
        .colorAttachmentCount = u32(desc.color_targets.count),
        .pColorAttachments = rendering_attachment_infos.data(),
        .pDepthAttachment = depth_attachment_exists ? &depth_attachment_info : nullptr,
    };

    vkCmdBeginRendering(s_ctx.command_buffers.get_ptr(desc.cb)->command_buffer, &rendering_info);
}

void rhi::cmd_end_rendering(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    vkCmdEndRendering(s_ctx.command_buffers.get_ptr(cb)->command_buffer);
}

void rhi::cmd_bind_index_buffer(
    rhi::CommandBuffer cb,
    rhi::Buffer buffer,
    u64 offset,
    rhi::IndexType index_type
) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(buffer);

    vkCmdBindIndexBuffer(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        s_ctx.buffers.get_ptr(buffer)->buffer,
        offset,
        index_type_to_vk(index_type)
    );
}

void rhi::cmd_draw(
    rhi::CommandBuffer cb,
    u32 vertex_count,
    u32 instance_count,
    u32 first_vertex,
    u32 first_instance
) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(vertex_count > 0);
    DEBUG_ASSERT(instance_count > 0);

    vkCmdDraw(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        vertex_count,
        instance_count,
        first_vertex,
        first_instance
    );
}

void rhi::cmd_draw_indexed(
    rhi::CommandBuffer cb,
    u32 index_count,
    u32 instance_count,
    u32 first_index,
    i32 vertex_offset,
    u32 first_instance
) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(index_count > 0);
    DEBUG_ASSERT(instance_count > 0);

    vkCmdDrawIndexed(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        index_count,
        instance_count,
        first_index,
        vertex_offset,
        first_instance
    );
}

void rhi::cmd_draw_indirect(
    CommandBuffer cb,
    Buffer buffer,
    u64 offset,
    u32 draw_count,
    u32 stride
) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(buffer);
    DEBUG_ASSERT(draw_count > 0);

    vkCmdDrawIndirect(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        s_ctx.buffers.get_ptr(buffer)->buffer,
        offset,
        draw_count,
        stride
    );
}

void rhi::cmd_draw_indexed_indirect_count(
    rhi::CommandBuffer cb,
    rhi::Buffer buffer,
    u64 offset,
    rhi::Buffer count_buffer,
    u64 count_buffer_offset,
    u32 max_draw_count,
    u32 stride
) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(buffer);
    DEBUG_ASSERT(count_buffer);
    DEBUG_ASSERT(max_draw_count > 0);

    vkCmdDrawIndexedIndirectCount(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        s_ctx.buffers.get_ptr(buffer)->buffer,
        offset,
        s_ctx.buffers.get_ptr(count_buffer)->buffer,
        count_buffer_offset,
        max_draw_count,
        stride
    );
}

void rhi::cmd_bind_texture_descriptor_set(CommandBuffer cb, rhi::Pipeline pipeline) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(pipeline);

    const PipelineImpl* const vk_pipeline = s_ctx.pipelines.get_ptr(pipeline);

    vkCmdBindDescriptorSets(
        s_ctx.command_buffers.get_ptr(cb)->command_buffer,
        get_pipeline_bind_point(*vk_pipeline),
        vk_pipeline->layout,
        1,
        1,
        &s_ctx.bindless_textures_descriptor_set,
        0,
        nullptr
    );
}

void rhi::create_swapchain(Vec2U32 size) {
    rhi::device_wait_idle();

    rhi::destroy_swapchain();

    VkSurfaceCapabilitiesKHR surface_capabilities{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        s_ctx.physical_device,
        s_ctx.surface,
        &surface_capabilities
    ));
    if (surface_capabilities.currentExtent.width != UINT32_MAX) {
        s_ctx.swapchain.extent = surface_capabilities.currentExtent;
    } else {
        s_ctx.swapchain.extent.width = math::clamp(
            size.x,
            surface_capabilities.minImageExtent.width,
            surface_capabilities.maxImageExtent.width
        );
        s_ctx.swapchain.extent.height = math::clamp(
            size.y,
            surface_capabilities.minImageExtent.height,
            surface_capabilities.maxImageExtent.height
        );
    }

    u32 surface_format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        s_ctx.physical_device,
        s_ctx.surface,
        &surface_format_count,
        nullptr
    );
    DEBUG_ASSERT(surface_format_count > 0);
    eastl::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        s_ctx.physical_device,
        s_ctx.surface,
        &surface_format_count,
        surface_formats.data()
    );

    // TODO: support HDR when this gets merged:
    // https://forums.developer.nvidia.com/t/vulkan-extensions-needed-for-hdr-is-missing/334268/13
    bool swapchain_surface_format_found = false;

    for (u32 i = 0; i < surface_format_count; ++i) {
        const VkFormat format = surface_formats[i].format;
        if ((format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM)
            && (surface_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
        {
            s_ctx.swapchain.surface_format = surface_formats[i];
            swapchain_surface_format_found = true;
            break;
        }
    }
    if (!swapchain_surface_format_found) {
        fprintf(stderr, "vulkan: failed to find a suitable swapchain surface format\n");
        ASSERT(0);
    }

    s_ctx.swapchain.min_image_count = eastl::max(3u, surface_capabilities.minImageCount);
    const u32 max_image_count = surface_capabilities.maxImageCount;
    if (surface_capabilities.maxImageCount > 0 && max_image_count < s_ctx.swapchain.min_image_count)
    {
        s_ctx.swapchain.min_image_count = max_image_count;
    }

    // The spec guarantees that at least one bit will be set.
    VkCompositeAlphaFlagBitsKHR surface_composite_alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        surface_composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    } else if (
        surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
    )
    {
        surface_composite_alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    } else if (
        surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
    )
    {
        surface_composite_alpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }

    const VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = s_ctx.surface,
        .minImageCount = s_ctx.swapchain.min_image_count,
        .imageFormat = s_ctx.swapchain.surface_format.format,
        .imageColorSpace = s_ctx.swapchain.surface_format.colorSpace,
        .imageExtent = s_ctx.swapchain.extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = surface_composite_alpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };

    VK_CHECK(
        vkCreateSwapchainKHR(s_ctx.device, &swapchain_info, nullptr, &s_ctx.swapchain.swapchain)
    );

    u32 swapchain_texture_count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(
        s_ctx.device,
        s_ctx.swapchain.swapchain,
        &swapchain_texture_count,
        nullptr
    ));
    eastl::vector<VkImage> images(swapchain_texture_count);
    VK_CHECK(vkGetSwapchainImagesKHR(
        s_ctx.device,
        s_ctx.swapchain.swapchain,
        &swapchain_texture_count,
        images.data()
    ));

    eastl::vector<TextureImpl> textures(swapchain_texture_count);

    for (u32 i = 0; i < swapchain_texture_count; ++i) {
        textures[i].image = images[i];
        textures[i].type = rhi::TEXTURE_TYPE_2D;
        textures[i].format = format_to_rhi(s_ctx.swapchain.surface_format.format);
        textures[i].dimensions = {s_ctx.swapchain.extent.width, s_ctx.swapchain.extent.height, 1};
    }

    // Creating image views for every swapchain image.
    VkImageViewCreateInfo image_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = s_ctx.swapchain.surface_format.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    for (u32 i = 0; i < swapchain_texture_count; ++i) {
        image_view_info.image = textures[i].image;
        VkImageView image_view{};
        VK_CHECK(vkCreateImageView(s_ctx.device, &image_view_info, nullptr, &image_view));
        textures[i].view = image_view;
    }

    s_ctx.swapchain.textures.resize(swapchain_texture_count);
    for (u32 i = 0; i < swapchain_texture_count; ++i) {
        s_ctx.swapchain.textures[i] = s_ctx.textures.create_handle(textures[i]);
    }

    s_ctx.swapchain.ready_to_present_semaphores.resize(swapchain_texture_count);
    for (u32 i = 0; i < swapchain_texture_count; ++i) {
        const VkSemaphoreCreateInfo semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        VK_CHECK(vkCreateSemaphore(
            s_ctx.device,
            &semaphore_info,
            nullptr,
            &s_ctx.swapchain.ready_to_present_semaphores[i]
        ));
        debug_name_object(
            s_ctx.device,
            VK_OBJECT_TYPE_SEMAPHORE,
            reinterpret_cast<u64>(s_ctx.swapchain.ready_to_present_semaphores[i]),
            "ready_to_present_semaphore"
        );
    }
}

void rhi::destroy_swapchain() {
    rhi::device_wait_idle();

    for (size_t i = 0; i < s_ctx.swapchain.textures.size(); ++i) {
        TextureImpl* const t = s_ctx.textures.get_ptr(s_ctx.swapchain.textures[i]);
        vkDestroyImageView(s_ctx.device, t->view, nullptr);
        vkDestroySemaphore(s_ctx.device, s_ctx.swapchain.ready_to_present_semaphores[i], nullptr);
        s_ctx.textures.destroy_handle(s_ctx.swapchain.textures[i]);
    }
    s_ctx.swapchain.textures.clear();
    vkDestroySwapchainKHR(s_ctx.device, s_ctx.swapchain.swapchain, nullptr);
    s_ctx.swapchain.swapchain = VK_NULL_HANDLE;
}

rhi::SwapchainResult rhi::acquire_next_swapchain_texture(rhi::Texture& swapchain_texture) {
    const VkResult vulkan_result = vkAcquireNextImageKHR(
        s_ctx.device,
        s_ctx.swapchain.swapchain,
        1'000'000'000,
        s_ctx.frames[s_ctx.frame_idx].image_acquire_semaphore,
        nullptr,
        &s_ctx.image_idx
    );

    swapchain_texture = s_ctx.swapchain.textures[s_ctx.image_idx];

    switch (vulkan_result) {
        case VK_SUCCESS:
            return rhi::SWAPCHAIN_SUCCESS;
        case VK_ERROR_OUT_OF_DATE_KHR:
            return rhi::SWAPCHAIN_OUT_OF_DATE;
        case VK_SUBOPTIMAL_KHR:
            return rhi::SWAPCHAIN_SUBOPTIMAL;
        default:
            return rhi::SWAPCHAIN_ERROR;
    }
}

rhi::Texture rhi::get_swapchain_texture(u32 idx) {
    DEBUG_ASSERT(idx < s_ctx.swapchain.textures.size());
    return s_ctx.swapchain.textures[idx];
}

rhi::SwapchainResult rhi::queue_present(Queue handle) {
    const VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &s_ctx.swapchain.ready_to_present_semaphores[s_ctx.image_idx],
        .swapchainCount = 1,
        .pSwapchains = &s_ctx.swapchain.swapchain,
        .pImageIndices = &s_ctx.image_idx,
    };

    const VkResult vulkan_result =
        vkQueuePresentKHR(s_ctx.queues.get_ptr(handle)->queue, &present_info);
    switch (vulkan_result) {
        case VK_SUCCESS:
            return rhi::SWAPCHAIN_SUCCESS;
        case VK_ERROR_OUT_OF_DATE_KHR:
            return rhi::SWAPCHAIN_OUT_OF_DATE;
        case VK_SUBOPTIMAL_KHR:
            return rhi::SWAPCHAIN_SUBOPTIMAL;
        default:
            return rhi::SWAPCHAIN_ERROR;
    }
}

void rhi::device_wait_idle() {
    VK_CHECK(vkDeviceWaitIdle(s_ctx.device));
}

void rhi::queue_wait_idle(rhi::Queue handle) {
    VK_CHECK(vkQueueWaitIdle(s_ctx.queues.get_ptr(handle)->queue));
}

void rhi::begin_new_frame(int frame_in_flight_idx) {
    DEBUG_ASSERT(frame_in_flight_idx >= 0);
    DEBUG_ASSERT(frame_in_flight_idx < rhi::FRAMES_IN_FLIGHT);

    s_ctx.frame_idx = frame_in_flight_idx;
}
