#include "Renderer.hpp"

#include "../Utils.hpp"
#include "../Math/Vec2.hpp"
#include "../Math/Mat4.hpp"

#include <stdio.h>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cgltf.h>
#include <ktx.h>
#include <ktxvulkan.h>

#define SDL_PRINT_ERROR(functionName) \
    fprintf(stderr, "%s:%d: " functionName " failed: %s\n", __FILE__, __LINE__, SDL_GetError())

static u32 GetDispatchSize(u32 size, u32 workgroupSize)
{
    DEBUG_ASSERT(size > 0);
    DEBUG_ASSERT(workgroupSize % 2 == 0);

    return (size + workgroupSize - 1) / workgroupSize;
}

bool Renderer::Init()
{
    // SDL.
    {
#ifdef __linux__
        if (!SDL_SetHint("SDL_VIDEO_DRIVER", "x11"))
        {
            fprintf(
                stderr,
                "WARNING: I've had some strange bugs on Wayland, including hangs, segfaults on "
                "cleanup (glfw), also RenderDoc doesn't work on Wayland. It seems that using "
                "X11 "
                "(or Xwayland) is better for now.\n"
            );
            SDL_PRINT_ERROR("SDL_SetHint(\"SDL_VIDEO_DRIVER\", \"x11\"");
            return false;
        }
#endif

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            SDL_PRINT_ERROR("SDL_InitSubSystem");
            return false;
        }

        VK_CHECK(volkInitialize());

        mWindow = SDL_CreateWindow(
            "renderer",
            800,
            600,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MOUSE_GRABBED
                | SDL_WINDOW_MOUSE_RELATIVE_MODE | SDL_WINDOW_FULLSCREEN
        );
        if (!mWindow)
        {
            SDL_PRINT_ERROR("SDL_CreateWindow");
            return false;
        }

        (void)SDL_SetWindowPosition(mWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        (void)SDL_SetWindowRelativeMouseMode(mWindow, true);
    }

    // Instance.
    {
        u32 vulkanApiVersion = 0;
        VK_CHECK(vkEnumerateInstanceVersion(&vulkanApiVersion));
        if (vulkanApiVersion < VK_API_VERSION_1_4)
        {
            fprintf(stderr, "vulkan: API version 1.4 is required\n");
            return false;
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "None";
        appInfo.applicationVersion = 1;
        appInfo.pEngineName = "None";
        appInfo.engineVersion = 1;
        appInfo.apiVersion = VK_API_VERSION_1_4;

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

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;
        instanceInfo.enabledExtensionCount = u32(requiredExtensions.size());
        instanceInfo.ppEnabledExtensionNames = requiredExtensions.data();

        VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &mInstance));

        volkLoadInstanceOnly(mInstance);
    }

    // Surface.
    if (!SDL_Vulkan_CreateSurface(mWindow, mInstance, nullptr, &mSurface))
    {
        SDL_PRINT_ERROR("SDL_Vulkan_CreateSurface ");
        return false;
    }

    const char* const requiredDeviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
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

            VkPhysicalDeviceSubgroupProperties subgroupProperties{};
            subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;

            VkPhysicalDeviceProperties2 physicalDeviceProperties{};
            physicalDeviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            physicalDeviceProperties.pNext = &subgroupProperties;
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
                        mSurface,
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
            VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
            rayTracingPipelineFeatures.sType
                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

            VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
            rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
            rayQueryFeatures.pNext = &rayTracingPipelineFeatures;

            VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures{};
            accelStructFeatures.sType
                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            accelStructFeatures.pNext = &rayQueryFeatures;

            VkPhysicalDeviceVulkan14Features vulkanFeatures14{};
            vulkanFeatures14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
            vulkanFeatures14.pNext = &accelStructFeatures;

            VkPhysicalDeviceVulkan13Features vulkanFeatures13{};
            vulkanFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            vulkanFeatures13.pNext = &vulkanFeatures14;

            VkPhysicalDeviceVulkan12Features vulkanFeatures12{};
            vulkanFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            vulkanFeatures12.pNext = &vulkanFeatures13;

            VkPhysicalDeviceVulkan11Features vulkanFeatures11{};
            vulkanFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            vulkanFeatures11.pNext = &vulkanFeatures12;

            VkPhysicalDeviceFeatures2 physicalDeviceFeatures{};
            physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            physicalDeviceFeatures.pNext = &vulkanFeatures11;

            vkGetPhysicalDeviceFeatures2(physicalDevice, &physicalDeviceFeatures);

            // TODO: maybe check out profiles, this is getting ridiculous.
            VkBool32 supportsRequiredFeatures = true;
            supportsRequiredFeatures
                &= physicalDeviceFeatures.features.vertexPipelineStoresAndAtomics;
            supportsRequiredFeatures &= vulkanFeatures14.pushDescriptor;
            supportsRequiredFeatures &= vulkanFeatures13.dynamicRendering;
            supportsRequiredFeatures &= vulkanFeatures13.synchronization2;
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
            supportsRequiredFeatures &= accelStructFeatures.accelerationStructure;
            supportsRequiredFeatures &= rayQueryFeatures.rayQuery;
            supportsRequiredFeatures &= rayTracingPipelineFeatures.rayTracingPipeline;

            // TODO: remove, use mesh shaders instead.
            supportsRequiredFeatures &= physicalDeviceFeatures.features.geometryShader;

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

        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(mPhysicalDevice, &properties);

        Utils::strlcpy(mGpuName, properties.properties.deviceName, sizeof(mGpuName));
        printf("GPU: %s\n", mGpuName);
    }

    // Logical device, queue.
    {
        // Already checked when picking a physical device.
        Vulkan::QueueInfo queueInfo = Vulkan::GetQueue(mPhysicalDevice, VK_QUEUE_GRAPHICS_BIT);

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
        rayTracingPipelineFeatures.sType
            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        rayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;

        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
        rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rayQueryFeatures.pNext = &rayTracingPipelineFeatures;
        rayQueryFeatures.rayQuery = VK_TRUE;

        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures{};
        accelStructFeatures.sType
            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        accelStructFeatures.pNext = &rayQueryFeatures;
        accelStructFeatures.accelerationStructure = VK_TRUE;

        VkPhysicalDeviceVulkan14Features vulkanFeatures14{};
        vulkanFeatures14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
        vulkanFeatures14.pNext = &accelStructFeatures;
        vulkanFeatures14.pushDescriptor = VK_TRUE;

        VkPhysicalDeviceVulkan13Features vulkanFeatures13{};
        vulkanFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkanFeatures13.pNext = &vulkanFeatures14;
        vulkanFeatures13.dynamicRendering = VK_TRUE;
        vulkanFeatures13.synchronization2 = VK_TRUE;

        VkPhysicalDeviceVulkan12Features vulkanFeatures12{};
        vulkanFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vulkanFeatures12.pNext = &vulkanFeatures13;
        vulkanFeatures12.scalarBlockLayout = VK_TRUE;
        vulkanFeatures12.shaderInt8 = VK_TRUE;
        vulkanFeatures12.storageBuffer8BitAccess = VK_TRUE;
        vulkanFeatures12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
        vulkanFeatures12.bufferDeviceAddress = VK_TRUE;
        vulkanFeatures12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        vulkanFeatures12.descriptorBindingPartiallyBound = VK_TRUE;
        vulkanFeatures12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        vulkanFeatures12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
        vulkanFeatures12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        vulkanFeatures12.runtimeDescriptorArray = VK_TRUE;
        vulkanFeatures12.drawIndirectCount = VK_TRUE;
        vulkanFeatures12.shaderFloat16 = VK_TRUE;
        vulkanFeatures12.samplerFilterMinmax = VK_TRUE;

        VkPhysicalDeviceVulkan11Features vulkanFeatures11{};
        vulkanFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        vulkanFeatures11.pNext = &vulkanFeatures12;
        vulkanFeatures11.shaderDrawParameters = VK_TRUE;
        vulkanFeatures11.storagePushConstant16 = VK_TRUE;
        vulkanFeatures11.storageBuffer16BitAccess = VK_TRUE;
        vulkanFeatures11.uniformAndStorageBuffer16BitAccess = VK_TRUE;

        VkPhysicalDeviceFeatures2 physicalDeviceFeatures{};
        physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        physicalDeviceFeatures.pNext = &vulkanFeatures11;
        physicalDeviceFeatures.features.multiDrawIndirect = VK_TRUE;
        physicalDeviceFeatures.features.shaderInt64 = VK_TRUE;
        physicalDeviceFeatures.features.vertexPipelineStoresAndAtomics = VK_TRUE;
        physicalDeviceFeatures.features.fragmentStoresAndAtomics = VK_TRUE;
        physicalDeviceFeatures.features.samplerAnisotropy = VK_TRUE;
        physicalDeviceFeatures.features.sampleRateShading = VK_TRUE;
        physicalDeviceFeatures.features.shaderInt16 = VK_TRUE;

        // TODO: remove, use mesh shaders instead.
        physicalDeviceFeatures.features.geometryShader = VK_TRUE;

        const f32 queuePriority = 1.0f;

        VkDeviceQueueCreateInfo deviceQueueInfo{};
        deviceQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        deviceQueueInfo.queueFamilyIndex = queueInfo.queueIdx;
        deviceQueueInfo.queueCount = 1;
        deviceQueueInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext = &physicalDeviceFeatures;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &deviceQueueInfo;
        deviceInfo.enabledExtensionCount = u32(ARRAY_SIZE(requiredDeviceExtensions));
        deviceInfo.ppEnabledExtensionNames = requiredDeviceExtensions;

        VK_CHECK(vkCreateDevice(mPhysicalDevice, &deviceInfo, nullptr, &mDevice));

        volkLoadDevice(mDevice);

        vkGetDeviceQueue(mDevice, queueInfo.familyIdx, queueInfo.queueIdx, &queueInfo.queue);
        mQueueInfo = queueInfo;
    }

    // VMA.
    {
        VmaAllocatorCreateInfo vmaAllocatorInfo{};
        vmaAllocatorInfo.device = mDevice;
        vmaAllocatorInfo.physicalDevice = mPhysicalDevice;
        vmaAllocatorInfo.instance = mInstance;
        vmaAllocatorInfo.vulkanApiVersion = VK_API_VERSION_1_4;
        vmaAllocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

        VmaVulkanFunctions vmaVulkanFunctions{};
        VK_CHECK(vmaImportVulkanFunctionsFromVolk(&vmaAllocatorInfo, &vmaVulkanFunctions));
        vmaAllocatorInfo.pVulkanFunctions = &vmaVulkanFunctions;

        VK_CHECK(vmaCreateAllocator(&vmaAllocatorInfo, &mVmaAllocator));
    }

    if (!CreateSwapchain())
    {
        return false;
    }

    // Buffers.
    {
        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            // NOTE: creating a host visible, coherent, device local buffer.
            // Should be always legal even on discrete GPUs if total allocated
            // size is less than 200 MB or so. But I don't care about the size,
            // since resizable BAR is somewhat widely supported.
            const bool result = Vulkan::CreateBuffer(
                mFrame[i].uniformBuffer,
                mDevice,
                mVmaAllocator,
                sizeof(UniformData),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                    | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                "UniformBuffer"
            );
            if (!result)
            {
                return false;
            }
        }

        if (!Vulkan::CreateBuffer(
                mIndirectCountBuffer,
                mDevice,
                mVmaAllocator,
                sizeof(u32),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                "IndirectCountBuffer"
            ))
        {
            return false;
        }
    }

    // Texture descriptor set layout.
    {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = 0;
        layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        layoutBinding.descriptorCount = MAX_DESCRIPTOR_COUNT;
        layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

        const VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
            | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
        bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        bindingFlagsInfo.bindingCount = 1;
        bindingFlagsInfo.pBindingFlags = &bindingFlags;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = &bindingFlagsInfo;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &layoutBinding;

        VK_CHECK(
            vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mTextureDescriptorSetLayout)

        );
    }

    // Visibility buffer pipeline.
    {
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportInfo{};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.viewportCount = 1;
        viewportInfo.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
        rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationInfo.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampleInfo{};
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
        depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilInfo.depthTestEnable = VK_TRUE;
        depthStencilInfo.depthWriteEnable = VK_TRUE;
        depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER;

        VkPipelineColorBlendAttachmentState colorBlendAttachments[1]{};
        colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT
            | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = ARRAY_SIZE(colorBlendAttachments);
        colorBlending.pAttachments = colorBlendAttachments;

        // clang-format off
            const VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };
        // clang-format on

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = u32(ARRAY_SIZE(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        const VkFormat colorAttachmentFormats[] = {mVisibilityImageFormat};

        VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
        pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingInfo.colorAttachmentCount = ARRAY_SIZE(colorAttachmentFormats);
        pipelineRenderingInfo.pColorAttachmentFormats = colorAttachmentFormats;
        pipelineRenderingInfo.depthAttachmentFormat = mDepthFormat;

        static_assert(ARRAY_SIZE(colorBlendAttachments) == ARRAY_SIZE(colorAttachmentFormats));

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &pipelineRenderingInfo;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineInfo.pViewportState = &viewportInfo;
        pipelineInfo.pRasterizationState = &rasterizationInfo;
        pipelineInfo.pMultisampleState = &multisampleInfo;
        pipelineInfo.pDepthStencilState = &depthStencilInfo;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = mRenderPipeline.layout;

        if (!Vulkan::CreateGraphicsPipeline(
                mVisibilityPipeline,
                mDevice,
                {"VisibilityBuffer.vert.hlsl.spv", "VisibilityBuffer.frag.hlsl.spv"},
                VK_NULL_HANDLE,
                pipelineInfo,
                "VisibilityBufferPass"
            ))
        {
            return false;
        }
    }

    // Fullscreen triangle pipeline.
    {
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportInfo{};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.viewportCount = 1;
        viewportInfo.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
        rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
        rasterizationInfo.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampleInfo{};
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
        depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        VkPipelineColorBlendAttachmentState colorBlendAttachments[1]{};
        colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT
            | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = ARRAY_SIZE(colorBlendAttachments);
        colorBlending.pAttachments = colorBlendAttachments;

        // clang-format off
            const VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };
        // clang-format on

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = u32(ARRAY_SIZE(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        const VkFormat colorAttachmentFormats[] = {mSwapchain.surfaceFormat.format};

        VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
        pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingInfo.colorAttachmentCount = ARRAY_SIZE(colorAttachmentFormats);
        pipelineRenderingInfo.pColorAttachmentFormats = colorAttachmentFormats;

        static_assert(ARRAY_SIZE(colorBlendAttachments) == ARRAY_SIZE(colorAttachmentFormats));

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &pipelineRenderingInfo;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineInfo.pViewportState = &viewportInfo;
        pipelineInfo.pRasterizationState = &rasterizationInfo;
        pipelineInfo.pMultisampleState = &multisampleInfo;
        pipelineInfo.pDepthStencilState = &depthStencilInfo;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = mRenderPipeline.layout;

        if (!Vulkan::CreateGraphicsPipeline(
                mFullscreenPipeline,
                mDevice,
                {"Fullscreen.vert.hlsl.spv", "Fullscreen.frag.hlsl.spv"},
                VK_NULL_HANDLE,
                pipelineInfo,
                "FullscreenPass"
            ))
        {
            return false;
        }
    }

    // Debug grad error pipeline.
    {
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportInfo{};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.viewportCount = 1;
        viewportInfo.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
        rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationInfo.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampleInfo{};
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
        depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilInfo.depthTestEnable = VK_TRUE;
        depthStencilInfo.depthWriteEnable = VK_TRUE;
        depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER;

        VkPipelineColorBlendAttachmentState colorBlendAttachments[1]{};
        colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT
            | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = ARRAY_SIZE(colorBlendAttachments);
        colorBlending.pAttachments = colorBlendAttachments;

        // clang-format off
            const VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };
        // clang-format on

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = u32(ARRAY_SIZE(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        const VkFormat colorAttachmentFormats[] = {mSwapchain.surfaceFormat.format};

        VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
        pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingInfo.colorAttachmentCount = ARRAY_SIZE(colorAttachmentFormats);
        pipelineRenderingInfo.pColorAttachmentFormats = colorAttachmentFormats;
        pipelineRenderingInfo.depthAttachmentFormat = mDepthFormat;

        static_assert(ARRAY_SIZE(colorBlendAttachments) == ARRAY_SIZE(colorAttachmentFormats));

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &pipelineRenderingInfo;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineInfo.pViewportState = &viewportInfo;
        pipelineInfo.pRasterizationState = &rasterizationInfo;
        pipelineInfo.pMultisampleState = &multisampleInfo;
        pipelineInfo.pDepthStencilState = &depthStencilInfo;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = mRenderPipeline.layout;

        if (!Vulkan::CreateGraphicsPipeline(
                mDebugGradErrorPipeline,
                mDevice,
                {"DebugGradError.vert.hlsl.spv", "DebugGradError.frag.hlsl.spv"},
                VK_NULL_HANDLE,
                pipelineInfo,
                "DebugGradErrorPass"
            ))
        {
            return false;
        }
    }

    // Compute pipelines.
    {
        {
            if (!Vulkan::CreateComputePipeline(
                    mCullPipeline,
                    mDevice,
                    "Cull.comp.hlsl.spv",
                    VK_NULL_HANDLE,
                    "CullPass"
                ))
            {
                return false;
            }
        }

        {
            if (!Vulkan::CreateComputePipeline(
                    mRenderPipeline,
                    mDevice,
                    "Renderer.comp.hlsl.spv",
                    mTextureDescriptorSetLayout,
                    "RenderPass"
                ))
            {
                return false;
            }
        }

        {
            if (!Vulkan::CreateComputePipeline(
                    mTaaResolvePipeline,
                    mDevice,
                    "TaaResolve.comp.hlsl.spv",
                    VK_NULL_HANDLE,
                    "TaaResolvePass"
                ))
            {
                return false;
            }
        }
    }

    // Command pool.
    {
        VkCommandPoolCreateInfo cmdPoolInfo{};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmdPoolInfo.queueFamilyIndex = mQueueInfo.familyIdx;
        VK_CHECK(vkCreateCommandPool(mDevice, &cmdPoolInfo, nullptr, &mCommandPool));
    }

    // Command buffers.
    {
        VkCommandBufferAllocateInfo cmdBufferAllocateInfo{};
        cmdBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufferAllocateInfo.commandPool = mCommandPool;
        cmdBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufferAllocateInfo.commandBufferCount = 1;

        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VK_CHECK(
                vkAllocateCommandBuffers(mDevice, &cmdBufferAllocateInfo, &mFrame[i].commandBuffer)
            );
        }
    }

    // Synchronization primitives.
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        mRenderFinishedSemaphores.resize(mSwapchain.images.size());
        for (VkSemaphore& sem : mRenderFinishedSemaphores)
        {
            VkSemaphore semaphore{};
            VK_CHECK(vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &semaphore));
            sem = semaphore;
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VK_CHECK(vkCreateFence(mDevice, &fenceInfo, nullptr, &mFrame[i].queueSubmitFence));
            VK_CHECK(vkCreateSemaphore(
                mDevice,
                &semaphoreInfo,
                nullptr,
                &mFrame[i].imageAcquireSemaphore
            ));
        }
    }

    // Samplers.
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 4.0f;
        samplerInfo.maxLod = 16.0f;
        VK_CHECK(vkCreateSampler(mDevice, &samplerInfo, nullptr, &mTextureSampler));

        samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(mDevice, &samplerInfo, nullptr, &mLinearSampler));
    }

    // Scene.
    {
        const std::string gltfPath = "../Assets/main_sponza/NewSponza_Main_glTF_003.gltf";

        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        std::vector<MeshPrimitive> meshPrimitives;
        std::vector<VkDrawIndexedIndirectCommand> drawCmds;
        std::vector<DrawData> drawData;
        std::vector<Material> materials;
        std::vector<std::string> texturePaths;

        if (!LoadScene(
                vertices,
                indices,
                meshPrimitives,
                drawCmds,
                drawData,
                materials,
                texturePaths,
                mUniformData.sunDirectionWorld,
                gltfPath
            ))
        {
            fprintf(stderr, "scene loading failed, path: %s\n", gltfPath.c_str());
            return false;
        }

        mUniformData.drawCount = u32(drawCmds.size());

        if (!UploadTextures(texturePaths))
        {
            fprintf(stderr, "vulkan: texture uploading failed\n");
            return false;
        }

        bool result = Vulkan::CreateBuffer(
            mVertexBuffer,
            mDevice,
            mVmaAllocator,
            VEC_SIZE_BYTES(vertices),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            "VertexBuffer"
        );
        if (!result)
        {
            return false;
        }
        memcpy(mVertexBuffer.mapped, vertices.data(), VEC_SIZE_BYTES(vertices));
        Vulkan::UnmapBuffer(mVertexBuffer, mVmaAllocator);

        result = Vulkan::CreateBuffer(
            mIndexBuffer,
            mDevice,
            mVmaAllocator,
            VEC_SIZE_BYTES(indices),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            "IndexBuffer"
        );
        if (!result)
        {
            return false;
        }
        memcpy(mIndexBuffer.mapped, indices.data(), VEC_SIZE_BYTES(indices));
        Vulkan::UnmapBuffer(mIndexBuffer, mVmaAllocator);

        result = Vulkan::CreateBuffer(
            mIndirectBuffer1,
            mDevice,
            mVmaAllocator,
            sizeof(VkDrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            "IndirectBuffer1"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mIndirectBuffer2,
            mDevice,
            mVmaAllocator,
            sizeof(VkDrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "IndirectBuffer2"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mDrawIndicesBuffer,
            mDevice,
            mVmaAllocator,
            sizeof(u32) * MAX_DRAW_CALLS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "DrawIndicesBuffer"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mMaterialBuffer,
            mDevice,
            mVmaAllocator,
            sizeof(Material) * MAX_DRAW_CALLS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            "MaterialBuffer"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mDrawDataBuffer,
            mDevice,
            mVmaAllocator,
            sizeof(DrawData) * MAX_DRAW_CALLS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            "DrawDataBuffer"
        );
        if (!result)
        {
            return false;
        }

        memcpy(mIndirectBuffer1.mapped, drawCmds.data(), VEC_SIZE_BYTES(drawCmds));

        memcpy(mMaterialBuffer.mapped, materials.data(), VEC_SIZE_BYTES(materials));

        memcpy(mDrawDataBuffer.mapped, drawData.data(), VEC_SIZE_BYTES(drawData));

        // TODO: would be nice if something like VK_NV_cluster_acceleration_structure
        // would become core eventually.
        if (!CreateAndUploadBlas(meshPrimitives, drawCmds))
        {
            printf("vulkan: BLAS creating and uploading failed\n");
            return false;
        }

        if (!CreateAndUploadTlas(meshPrimitives, drawData))
        {
            printf("vulkan: TLAS creating and uploading failed\n");
            return false;
        }
    }

    // Descriptor pool, descriptor set.
    {
        const VkDescriptorPoolSize poolSizes[] = {
            {
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                MAX_DESCRIPTOR_COUNT,
            },
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = ARRAY_SIZE(poolSizes);
        poolInfo.pPoolSizes = poolSizes;
        VK_CHECK(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mDescriptorPool));

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = mDescriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &mTextureDescriptorSetLayout;

        VK_CHECK(vkAllocateDescriptorSets(mDevice, &allocateInfo, &mTextureDescriptorSet));

        for (size_t i = 0; i < mTextures.size(); ++i)
        {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageView = mTextures[i].view;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writeSet{};
            writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeSet.dstSet = mTextureDescriptorSet;
            writeSet.dstBinding = 0;
            writeSet.dstArrayElement = u32(i);
            writeSet.descriptorCount = 1;
            writeSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writeSet.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(mDevice, 1, &writeSet, 0, nullptr);
        }
    }

    if (!mImguiRenderer.Init(
            mWindow,
            mPhysicalDevice,
            mDevice,
            mVmaAllocator,
            mCommandPool,
            mQueueInfo,
            mSwapchain.surfaceFormat.format
        ))
    {
        fprintf(stderr, "Failed to initialize ImGui renderer\n");
        return false;
    }

    mSwapchainNeedsRecreating = true;
    mTaaJitterMaxIdx = 8;
    mUniformData.taaBlendWeight = 0.1f;
    mUniformData.ambientIntensity = 0.1f;
    mUniformData.sunIntensity = 5.0f;
    mUniformData.gradErrorMax = 1e-3f;

    return true;
}

void Renderer::Cleanup()
{
    if (!mDevice)
    {
        return;
    }

    (void)vkDeviceWaitIdle(mDevice);

    mImguiRenderer.Cleanup();

    CleanupColorResources();
    CleanupDepthResources();

    for (Vulkan::Image& tex : mTextures)
    {
        Vulkan::DestroyImage(tex, mDevice, mVmaAllocator);
    }

    for (VkAccelerationStructureKHR as : mBlas)
    {
        vkDestroyAccelerationStructureKHR(mDevice, as, nullptr);
    }
    vkDestroyAccelerationStructureKHR(mDevice, mTlas, nullptr);
    Vulkan::DestroyBuffer(mTlasBuffer, mVmaAllocator);
    Vulkan::DestroyBuffer(mBlasBuffer, mVmaAllocator);
    Vulkan::DestroyBuffer(mIndirectCountBuffer, mVmaAllocator);
    Vulkan::DestroyBuffer(mMaterialBuffer, mVmaAllocator);
    Vulkan::DestroyBuffer(mDrawDataBuffer, mVmaAllocator);
    Vulkan::DestroyBuffer(mVertexBuffer, mVmaAllocator);
    Vulkan::DestroyBuffer(mIndexBuffer, mVmaAllocator);
    Vulkan::DestroyBuffer(mDrawIndicesBuffer, mVmaAllocator);
    Vulkan::DestroyBuffer(mIndirectBuffer2, mVmaAllocator);
    Vulkan::DestroyBuffer(mIndirectBuffer1, mVmaAllocator);
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        Vulkan::DestroyBuffer(mFrame[i].uniformBuffer, mVmaAllocator);
    }
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroyFence(mDevice, mFrame[i].queueSubmitFence, nullptr);
        vkDestroySemaphore(mDevice, mFrame[i].imageAcquireSemaphore, nullptr);
    }
    for (VkSemaphore sem : mRenderFinishedSemaphores)
    {
        vkDestroySemaphore(mDevice, sem, nullptr);
    }
    vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);
    vkDestroySampler(mDevice, mLinearSampler, nullptr);
    vkDestroySampler(mDevice, mTextureSampler, nullptr);
    vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
    vkDestroyDescriptorSetLayout(mDevice, mTextureDescriptorSetLayout, nullptr);
    Vulkan::DestroyPipeline(mDebugGradErrorPipeline, mDevice);
    Vulkan::DestroyPipeline(mTaaResolvePipeline, mDevice);
    Vulkan::DestroyPipeline(mCullPipeline, mDevice);
    Vulkan::DestroyPipeline(mFullscreenPipeline, mDevice);
    Vulkan::DestroyPipeline(mRenderPipeline, mDevice);
    Vulkan::DestroyPipeline(mVisibilityPipeline, mDevice);
    CleanupSwapchain();
    vmaDestroyAllocator(mVmaAllocator);
    vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
    vkDestroyDevice(mDevice, nullptr);
    vkDestroyInstance(mInstance, nullptr);
    volkFinalize();
}

bool Renderer::StartNewFrame()
{
    DEBUG_ASSERT(!mNewFrameStarted);

    Frame& frame = mFrame[mFrameIdx];

    VK_CHECK(vkWaitForFences(mDevice, 1, &frame.queueSubmitFence, VK_TRUE, 1'000'000'000));
    VK_CHECK(vkResetFences(mDevice, 1, &frame.queueSubmitFence));

    mImguiRenderer.StartNewFrame();

    mNewFrameStarted = true;

    return true;
}

bool Renderer::Render(f32 deltaTime)
{
    DEBUG_ASSERT(deltaTime > 0.0f);

    if (mRenderingPaused)
    {
        SDL_Delay(100);
        return true;
    }

    if (mSwapchainNeedsRecreating)
    {
        mSwapchainNeedsRecreating = false;

        if (!CreateSwapchain())
        {
            return false;
        }
    }

    Frame& frame = mFrame[mFrameIdx];

    u32 imageIdx = 0;
    VkResult vulkanResult = vkAcquireNextImageKHR(
        mDevice,
        mSwapchain.swapchain,
        1'000'000'000,
        frame.imageAcquireSemaphore,
        nullptr,
        &imageIdx
    );
    if (vulkanResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        mSwapchainNeedsRecreating = true;
        return true;
    }
    else if (vulkanResult != VK_SUCCESS && vulkanResult != VK_SUBOPTIMAL_KHR)
    {
        VK_CHECK_PRINT_ERROR(vulkanResult);
        return false;
    }

    mUniformData.deltaTime = deltaTime;

    if (mUniformData.taaEnable == 1)
    {
        const f32 haltonX = 2.0f * HaltonSequence(mTaaJitterIdx + 1, 2) - 1.0f;
        const f32 haltonY = 2.0f * HaltonSequence(mTaaJitterIdx + 1, 3) - 1.0f;
        mUniformData.prevTaaJitter = mUniformData.taaJitter;
        mUniformData.taaJitter = {
            haltonX / (f32(mUniformData.renderWidth) * 2.0f),
            haltonY / (f32(mUniformData.renderHeight) * 2.0f),
        };

        mTaaJitterIdx = (mTaaJitterIdx + 1) % mTaaJitterMaxIdx;
        // To derive, construct a translation jitter matrix and multiply with viewToClip.
        mUniformData.viewToClip(0, 2) = -mUniformData.taaJitter.X();
        mUniformData.viewToClip(1, 2) = -mUniformData.taaJitter.Y();
    }
    else
    {
        mUniformData.viewToClip(0, 2) = 0.0f;
        mUniformData.viewToClip(1, 2) = 0.0f;
    }

    mUniformData.prevWorldToClip = mUniformData.worldToClip;
    mUniformData.worldToClip = mUniformData.viewToClip * mUniformData.worldToView;
    mUniformData.clipToWorld = Inverse(mUniformData.worldToClip);

    mUniformData.sunDirectionView
        = (mUniformData.worldToView * Vec4{mUniformData.sunDirectionWorld, 0.0f}).XYZ();
    mUniformData.sunDirectionView = Normalize(mUniformData.sunDirectionView);

    // https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/
    // Also, niagara:
    // https://github.com/zeux/niagara
    if (!mCullCameraFrozen)
    {
        const Mat4 viewToClipT = Transpose(mUniformData.viewToClip);

        // -w <= x; x + w >= 0
        const Vec4 frustumPlaneX = NormalizePlane(viewToClipT.col[0] + viewToClipT.col[3]);
        // -w <= y; y + w >= 0
        const Vec4 frustumPlaneY = NormalizePlane(viewToClipT.col[1] + viewToClipT.col[3]);

        mUniformData.cullFrustumPlaneXX = frustumPlaneX.X();
        mUniformData.cullFrustumPlaneXZ = frustumPlaneX.Z();
        mUniformData.cullFrustumPlaneYY = frustumPlaneY.Y();
        mUniformData.cullFrustumPlaneYZ = frustumPlaneY.Z();

        mUniformData.cullWorldToView = mUniformData.worldToView;
    }

    memcpy(frame.uniformBuffer.mapped, &mUniformData, sizeof(mUniformData));

    switch (mRenderMode)
    {
    case RenderMode::Normal:
        if (!RecordCommandBuffer(imageIdx))
        {
            return false;
        }
        break;
    case RenderMode::GradError:
        if (!RecordDebugGradErrorCommandBuffer(imageIdx))
        {
            return false;
        }
        break;
    }

    constexpr VkPipelineStageFlags waitDstStageMask
        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAcquireSemaphore;
    submitInfo.pWaitDstStageMask = &waitDstStageMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &mRenderFinishedSemaphores[imageIdx];
    VK_CHECK(vkQueueSubmit(mQueueInfo.queue, 1, &submitInfo, frame.queueSubmitFence));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &mRenderFinishedSemaphores[imageIdx];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &mSwapchain.swapchain;
    presentInfo.pImageIndices = &imageIdx;
    vulkanResult = vkQueuePresentKHR(mQueueInfo.queue, &presentInfo);
    if (vulkanResult == VK_ERROR_OUT_OF_DATE_KHR || vulkanResult == VK_SUBOPTIMAL_KHR)
    {
        mSwapchainNeedsRecreating = true;
    }
    else if (vulkanResult != VK_SUCCESS)
    {
        VK_CHECK_PRINT_ERROR(vulkanResult);
        return false;
    }

    mNewFrameStarted = false;

    mPrevFrameIdx = mFrameIdx;
    mFrameIdx = (mFrameIdx + 1) % RENDERER_MAX_FRAMES_IN_FLIGHT;
    ++mUniformData.frameCount;
    mSwapchainRecreated = false;
    mRenderModeChanged = false;

    return true;
}

void Renderer::UpdateCamera(Vec3 position, const Mat4& worldToView)
{
    mUniformData.cameraPosition = position;
    mUniformData.worldToView = worldToView;
}

void Renderer::PauseRendering(bool paused)
{
    mRenderingPaused = paused;
}

void Renderer::ChangeRenderMode(RenderMode mode)
{
    if (mRenderMode != mode)
    {
        mRenderModeChanged = true;
        mRenderMode = mode;
    }
}

void Renderer::FreezeCullCamera(bool frozen)
{
    mCullCameraFrozen = frozen;
}

bool Renderer::UploadTextures(const std::vector<std::string>& texturePaths)
{
    DEBUG_ASSERT(!texturePaths.empty());

    mTextures.resize(texturePaths.size());

    for (size_t i = 0; i < texturePaths.size(); ++i)
    {
        ktxTexture2* ktxTex{};
        ktx_error_code_e ktxResult = ktxTexture2_CreateFromNamedFile(
            texturePaths[i].c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &ktxTex
        );
        if (ktxResult != KTX_SUCCESS)
        {
            fprintf(
                stderr,
                "ktx2 %s loading error: %s\n",
                texturePaths[i].c_str(),
                ktxErrorString(ktxResult)
            );
            return false;
        }
        DEFER(ktxTexture2_Destroy(ktxTex));

        const VkFormat format = ktxTexture2_GetVkFormat(ktxTex);
        VkExtent3D extent{ktxTex->baseWidth, ktxTex->baseHeight, ktxTex->baseDepth};
        const u32 mipLevels = ktxTex->numLevels;
        const ktx_size_t size = ktxTexture_GetDataSize(ktxTexture(ktxTex));
        const ktx_uint8_t* ktxData = ktxTexture_GetData(ktxTexture(ktxTex));

        if (!Vulkan::CreateImage(
                mTextures[i],
                mDevice,
                mVmaAllocator,
                format,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                ktxTex->baseWidth,
                ktxTex->baseHeight,
                1,
                texturePaths[i].c_str(),
                mipLevels
            ))
        {
            return false;
        }

        Vulkan::Buffer stagingBuffer{};
        if (!Vulkan::CreateBuffer(
                stagingBuffer,
                mDevice,
                mVmaAllocator,
                size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                "stagingBuffer"
            ))
        {
            return false;
        }
        DEFER(Vulkan::DestroyBuffer(stagingBuffer, mVmaAllocator));

        std::vector<VkBufferImageCopy> copyRegions(mipLevels);

        for (u32 mipLevel = 0; mipLevel < mipLevels; ++mipLevel)
        {
            ktx_size_t offset = 0;
            ktxResult = ktxTexture2_GetImageOffset(ktxTex, mipLevel, 0, 0, &offset);
            if (ktxResult != KTX_SUCCESS)
            {
                fprintf(
                    stderr,
                    "ktxTexture_GetImageOffset failed: %s\n",
                    ktxErrorString(ktxResult)
                );
                return false;
            }

            VkBufferImageCopy& region = copyRegions[mipLevel];
            region.bufferOffset = offset;
            region.imageSubresource.layerCount = 1;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mipLevel;
            region.imageExtent.width = extent.width >> mipLevel;
            region.imageExtent.height = extent.height >> mipLevel;
            region.imageExtent.depth = 1;
        }
        memcpy(stagingBuffer.mapped, ktxData, size);
        Vulkan::UnmapBuffer(stagingBuffer, mVmaAllocator);

        const VkCommandBuffer cmd = mFrame[0].commandBuffer;

        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

        Vulkan::CmdImageMemoryBarrier(
            cmd,
            {
                Vulkan::ImageMemoryBarrier(
                    mTextures[i].image,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE,
                    VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    mipLevels
                ),
            }
        );

        vkCmdCopyBufferToImage(
            cmd,
            stagingBuffer.buffer,
            mTextures[i].image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            u32(copyRegions.size()),
            copyRegions.data()
        );

        Vulkan::CmdImageMemoryBarrier(
            cmd,
            {
                Vulkan::ImageMemoryBarrier(
                    mTextures[i].image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_NONE,
                    VK_ACCESS_2_NONE,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    mipLevels
                ),
            }
        );

        VK_CHECK(vkEndCommandBuffer(cmd));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        VK_CHECK(vkQueueSubmit(mQueueInfo.queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(mQueueInfo.queue));
    }

    return true;
}

bool Renderer::CreateAndUploadBlas(
    const std::vector<MeshPrimitive>& meshPrimitives,
    const std::vector<VkDrawIndexedIndirectCommand>& drawCmds
)
{
    DEBUG_ASSERT(!meshPrimitives.empty());
    DEBUG_ASSERT(!drawCmds.empty());
    DEBUG_ASSERT(meshPrimitives.size() == drawCmds.size());

    std::vector<VkAccelerationStructureGeometryKHR> geometries(meshPrimitives.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos(meshPrimitives.size());
    std::vector<u32> primitiveCounts(meshPrimitives.size());
    std::vector<size_t> accelerationOffsets(meshPrimitives.size());
    std::vector<size_t> accelerationSizes(meshPrimitives.size());
    std::vector<size_t> scratchOffsets(meshPrimitives.size());

    const size_t ALIGNMENT = 256;

    size_t totalAccelerationSize = 0;
    size_t totalPrimitiveCount = 0;
    size_t totalScratchSize = 0;

    for (size_t i = 0; i < meshPrimitives.size(); ++i)
    {
        const MeshPrimitive& primitive = meshPrimitives[i];
        const VkDrawIndexedIndirectCommand& drawCmd = drawCmds[i];
        VkAccelerationStructureGeometryKHR& geometry = geometries[i];
        VkAccelerationStructureBuildGeometryInfoKHR& buildInfo = buildInfos[i];

        ASSERT(primitive.vertexCount > 0);

        primitiveCounts[i] = drawCmd.indexCount / 3;

        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles.sType
            = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        geometry.geometry.triangles.vertexData.deviceAddress
            = mVertexBuffer.deviceAddress + size_t(drawCmd.vertexOffset) * sizeof(Vertex);
        geometry.geometry.triangles.vertexStride = sizeof(Vertex);
        geometry.geometry.triangles.maxVertex = u32(primitive.vertexCount - 1);
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geometry.geometry.triangles.indexData.deviceAddress
            = mIndexBuffer.deviceAddress + drawCmd.firstIndex * sizeof(u32);
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(
            mDevice,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo,
            &primitiveCounts[i],
            &sizeInfo
        );

        accelerationOffsets[i] = totalAccelerationSize;
        accelerationSizes[i] = sizeInfo.accelerationStructureSize;
        scratchOffsets[i] = totalScratchSize;

        totalAccelerationSize = Utils::AlignUpPow2(
            totalAccelerationSize + sizeInfo.accelerationStructureSize,
            ALIGNMENT
        );
        totalScratchSize
            = Utils::AlignUpPow2(totalScratchSize + sizeInfo.buildScratchSize, ALIGNMENT);
        totalPrimitiveCount += primitiveCounts[i];
    }

    if (!Vulkan::CreateBuffer(
            mBlasBuffer,
            mDevice,
            mVmaAllocator,
            totalAccelerationSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "BlasBuffer"
        ))
    {
        return false;
    }

    Vulkan::Buffer scratchBuffer{};
    if (!Vulkan::CreateBuffer(
            scratchBuffer,
            mDevice,
            mVmaAllocator,
            totalScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "ScratchBuffer",
            ALIGNMENT
        ))
    {
        return false;
    }
    DEFER(Vulkan::DestroyBuffer(scratchBuffer, mVmaAllocator));

    printf(
        "BLAS accelerationStructureSize: %.2f MB, scratchSize: %.2f MB, %.3fM "
        "triangles\n",
        f64(totalAccelerationSize) / 1.0e6,
        f64(totalScratchSize) / 1.0e6,
        f64(totalPrimitiveCount) / 1.0e6
    );

    mBlas.resize(meshPrimitives.size());

    for (size_t i = 0; i < meshPrimitives.size(); ++i)
    {
        VkAccelerationStructureCreateInfoKHR accelerationInfo{};
        accelerationInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        accelerationInfo.buffer = mBlasBuffer.buffer;
        accelerationInfo.offset = accelerationOffsets[i];
        accelerationInfo.size = accelerationSizes[i];
        accelerationInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        VK_CHECK(vkCreateAccelerationStructureKHR(mDevice, &accelerationInfo, nullptr, &mBlas[i]));
    }

    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges(meshPrimitives.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> buildRangePtrs(
        meshPrimitives.size()
    );

    for (size_t i = 0; i < meshPrimitives.size(); ++i)
    {
        buildInfos[i].dstAccelerationStructure = mBlas[i];
        buildInfos[i].scratchData.deviceAddress = scratchBuffer.deviceAddress + scratchOffsets[i];

        buildRanges[i].primitiveCount = primitiveCounts[i];
        buildRangePtrs[i] = &buildRanges[i];
    }

    const VkCommandBuffer cmd = mFrame[0].commandBuffer;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    vkCmdBuildAccelerationStructuresKHR(
        cmd,
        u32(buildInfos.size()),
        buildInfos.data(),
        buildRangePtrs.data()
    );

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(mQueueInfo.queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkDeviceWaitIdle(mDevice));

    return true;
}

bool Renderer::CreateAndUploadTlas(
    const std::vector<MeshPrimitive>& meshPrimitives,
    const std::vector<DrawData>& drawData
)
{
    DEBUG_ASSERT(!meshPrimitives.empty());
    DEBUG_ASSERT(!drawData.empty());

    const size_t ALIGNMENT = 256;
    const VkCommandBuffer cmd = mFrame[0].commandBuffer;

    Vulkan::Buffer instances{};
    if (!Vulkan::CreateBuffer(
            instances,
            mDevice,
            mVmaAllocator,
            sizeof(VkAccelerationStructureInstanceKHR) * meshPrimitives.size(),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "InstanceBuffer"
        ))
    {
        return false;
    }
    DEFER(Vulkan::DestroyBuffer(instances, mVmaAllocator));

    std::vector<VkDeviceAddress> blasAddresses(mBlas.size());

    for (size_t i = 0; i < mBlas.size(); ++i)
    {
        VkAccelerationStructureDeviceAddressInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        info.accelerationStructure = mBlas[i];
        blasAddresses[i] = vkGetAccelerationStructureDeviceAddressKHR(mDevice, &info);
        if (!blasAddresses[i])
        {
            fprintf(stderr, "vkGetAccelerationStructureDeviceAddressKHR failed, idx = %zu\n", i);
            return false;
        }
    }

    for (size_t i = 0; i < drawData.size(); ++i)
    {
        const Mat4 transform
            = Transpose(Model(drawData[i].position, drawData[i].orientation, drawData[i].scale));

        VkAccelerationStructureInstanceKHR instance{};
        memcpy(
            instance.transform.matrix[0],
            &transform.col[0].val[0],
            sizeof(instance.transform.matrix[0])
        );
        memcpy(
            instance.transform.matrix[1],
            &transform.col[1].val[0],
            sizeof(instance.transform.matrix[1])
        );
        memcpy(
            instance.transform.matrix[2],
            &transform.col[2].val[0],
            sizeof(instance.transform.matrix[2])
        );
        instance.instanceCustomIndex = u32(i);
        instance.mask = 0xff;
        instance.accelerationStructureReference = blasAddresses[i];
        instance.flags = drawData[i].renderPassFlags == RENDER_PASS_OPAQUE_BIT
            ? VkGeometryInstanceFlagsKHR(VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR)
            : VkGeometryInstanceFlagsKHR(VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR);

        memcpy(
            static_cast<VkAccelerationStructureInstanceKHR*>(instances.mapped) + i,
            &instance,
            sizeof(VkAccelerationStructureInstanceKHR)
        );
    }

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType
        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = instances.deviceAddress;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    const u32 primitiveCount = u32(meshPrimitives.size());

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        mDevice,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo
    );

    printf(
        "TLAS accelerationStructureSize: %.2f MB, scratchSize: %.2f MB\n",
        f64(sizeInfo.accelerationStructureSize) / 1.0e6,
        f64(sizeInfo.buildScratchSize) / 1.0e6
    );

    if (!Vulkan::CreateBuffer(
            mTlasBuffer,
            mDevice,
            mVmaAllocator,
            sizeInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "TlasBuffer"
        ))
    {
        return false;
    }

    Vulkan::Buffer scratchBuffer{};
    if (!Vulkan::CreateBuffer(
            scratchBuffer,
            mDevice,
            mVmaAllocator,
            sizeInfo.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "ScratchBuffer",
            ALIGNMENT
        ))
    {
        return false;
    }
    DEFER(Vulkan::DestroyBuffer(scratchBuffer, mVmaAllocator));

    VkAccelerationStructureCreateInfoKHR accelerationInfo{};
    accelerationInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationInfo.buffer = mTlasBuffer.buffer;
    accelerationInfo.size = sizeInfo.accelerationStructureSize;
    accelerationInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    VK_CHECK(vkCreateAccelerationStructureKHR(mDevice, &accelerationInfo, nullptr, &mTlas));

    buildInfo.dstAccelerationStructure = mTlas;
    buildInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR buildRange{};
    buildRange.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* const buildRangePtr = &buildRange;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo{};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &buildRangePtr);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(mQueueInfo.queue, 1, &submitInfo, VK_NULL_HANDLE));

    VK_CHECK(vkDeviceWaitIdle(mDevice));

    return true;
}

void Renderer::VisibilityBufferPass(VkCommandBuffer cmd)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mVisibilityPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
        mVisibilityPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mDrawIndicesBuffer.buffer,
            mDrawDataBuffer.buffer,
            mVertexBuffer.buffer,
        }
    );

    VkViewport viewport{};
    viewport.width = f32(mRenderImageExtent.width);
    viewport.height = -f32(mRenderImageExtent.height);
    viewport.y = f32(mRenderImageExtent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = mRenderImageExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkRenderingAttachmentInfo renderingAttachmentInfos[1]{};

    renderingAttachmentInfos[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    renderingAttachmentInfos[0].imageView = mVisibilityImage.view;
    renderingAttachmentInfos[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    renderingAttachmentInfos[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    renderingAttachmentInfos[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachmentInfo{};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = mDepthImage.view;
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = mRenderImageExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = ARRAY_SIZE(renderingAttachmentInfos);
    renderingInfo.pColorAttachments = renderingAttachmentInfos;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdBindIndexBuffer(cmd, mIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirectCount(
        cmd,
        mIndirectBuffer2.buffer,
        0,
        mIndirectCountBuffer.buffer,
        0,
        mUniformData.drawCount,
        sizeof(VkDrawIndexedIndirectCommand)
    );

    vkCmdEndRendering(cmd);
}

void Renderer::CullPass(VkCommandBuffer cmd)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mCullPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
        mCullPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mDrawDataBuffer.buffer,
            mIndirectCountBuffer.buffer,
            mIndirectBuffer1.buffer,
            mIndirectBuffer2.buffer,
            mDrawIndicesBuffer.buffer,
        }
    );

    vkCmdDispatch(cmd, GetDispatchSize(mUniformData.drawCount, RENDERER_CULL_WORKGROUP_SIZE), 1, 1);
}

void Renderer::RenderPass(VkCommandBuffer cmd)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mRenderPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
        mRenderPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mDrawIndicesBuffer.buffer,
            mIndirectBuffer2.buffer,
            mDrawDataBuffer.buffer,
            mIndexBuffer.buffer,
            mVertexBuffer.buffer,
            mMaterialBuffer.buffer,
            mTextureSampler,
            mTlas,
            {mVisibilityImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mVelocityImage.view, VK_IMAGE_LAYOUT_GENERAL},
            {mRenderImage.view, VK_IMAGE_LAYOUT_GENERAL},
        }
    );

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        mRenderPipeline.layout,
        1,
        1,
        &mTextureDescriptorSet,
        0,
        nullptr
    );

    vkCmdDispatch(
        cmd,
        GetDispatchSize(mRenderImageExtent.width, RENDERER_RENDER_WORKGROUP_SIZE_X),
        GetDispatchSize(mRenderImageExtent.height, RENDERER_RENDER_WORKGROUP_SIZE_Y),
        1
    );
}

void Renderer::TaaResolvePass(VkCommandBuffer cmd)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mTaaResolvePipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
        mTaaResolvePipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            {mRenderImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mDepthImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mVelocityImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {
                mSwapchainRecreated || mRenderModeChanged
                    ? mFrame[mFrameIdx].resolvedRenderImage.view
                    : mFrame[mPrevFrameIdx].resolvedRenderImage.view,
                VK_IMAGE_LAYOUT_GENERAL,
            },
            {mFrame[mFrameIdx].resolvedRenderImage.view, VK_IMAGE_LAYOUT_GENERAL},
            mLinearSampler,
        }
    );

    vkCmdDispatch(
        cmd,
        GetDispatchSize(mRenderImageExtent.width, RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_X),
        GetDispatchSize(mRenderImageExtent.height, RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_Y),
        1
    );
}

void Renderer::FullscreenPass(VkCommandBuffer cmd, u32 imageIdx)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mFullscreenPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
        mFullscreenPipeline,
        {
            {mFrame[mFrameIdx].resolvedRenderImage.view, VK_IMAGE_LAYOUT_GENERAL},
            mLinearSampler,
        }
    );

    VkViewport viewport{};
    viewport.width = f32(mSwapchain.extent.width);
    viewport.height = -f32(mSwapchain.extent.height);
    viewport.y = f32(mSwapchain.extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = mSwapchain.extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkRenderingAttachmentInfo renderingAttachmentInfo{};
    renderingAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    renderingAttachmentInfo.imageView = mSwapchain.images[imageIdx].view;
    renderingAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    renderingAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    renderingAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = mSwapchain.extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &renderingAttachmentInfo;

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);
}

bool Renderer::RecordDebugGradErrorCommandBuffer(u32 imageIdx)
{
    Frame& frame = mFrame[mFrameIdx];

    const VkCommandBuffer cmd = frame.commandBuffer;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo{};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cmd, mIndirectCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    CullPass(cmd);

    Vulkan::CmdBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
        }
    );

    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mDebugGradErrorPipeline.pipeline);

        Vulkan::CmdPushDescriptors(
            cmd,
            mDebugGradErrorPipeline,
            {
                mFrame[mFrameIdx].uniformBuffer.buffer,
                mDrawIndicesBuffer.buffer,
                mIndirectBuffer2.buffer,
                mDrawDataBuffer.buffer,
                mIndexBuffer.buffer,
                mVertexBuffer.buffer,
            }
        );

        VkViewport viewport{};
        viewport.width = f32(mSwapchain.extent.width);
        viewport.height = -f32(mSwapchain.extent.height);
        viewport.y = f32(mSwapchain.extent.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = mSwapchain.extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkRenderingAttachmentInfo renderingAttachmentInfos[1]{};

        renderingAttachmentInfos[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        renderingAttachmentInfos[0].imageView = mSwapchain.images[imageIdx].view;
        renderingAttachmentInfos[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        renderingAttachmentInfos[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        renderingAttachmentInfos[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingAttachmentInfo depthAttachmentInfo{};
        depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachmentInfo.imageView = mDepthImage.view;
        depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.extent = mSwapchain.extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = ARRAY_SIZE(renderingAttachmentInfos);
        renderingInfo.pColorAttachments = renderingAttachmentInfos;
        renderingInfo.pDepthAttachment = &depthAttachmentInfo;

        vkCmdBeginRendering(cmd, &renderingInfo);

        vkCmdBindIndexBuffer(cmd, mIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexedIndirectCount(
            cmd,
            mIndirectBuffer2.buffer,
            0,
            mIndirectCountBuffer.buffer,
            0,
            mUniformData.drawCount,
            sizeof(VkDrawIndexedIndirectCommand)
        );

        vkCmdEndRendering(cmd);
    }

    if (!mImguiRenderer.UpdateVertexIndexBuffers(static_cast<u32>(mFrameIdx)))
    {
        return false;
    }

    if (mEnableUI)
    {
        VkRenderingAttachmentInfo renderingAttachmentInfo{};
        renderingAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        renderingAttachmentInfo.imageView = mSwapchain.images[imageIdx].view;
        renderingAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        renderingAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        renderingAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.extent = mSwapchain.extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &renderingAttachmentInfo;

        vkCmdBeginRendering(cmd, &renderingInfo);

        if (!mImguiRenderer.Render(cmd, u32(mFrameIdx)))
        {
            return false;
        }

        vkCmdEndRendering(cmd);
    }

    Vulkan::CmdImageMemoryBarrier(
        cmd,
        {
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE
            ),
        }
    );

    VK_CHECK(vkEndCommandBuffer(cmd));

    return true;
}

bool Renderer::RecordCommandBuffer(u32 imageIdx)
{
    Frame& frame = mFrame[mFrameIdx];

    const VkCommandBuffer cmd = frame.commandBuffer;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo{};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cmd, mIndirectCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    CullPass(cmd);

    Vulkan::CmdBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mVisibilityImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mGradImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
        }
    );

    VisibilityBufferPass(cmd);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
        {
            Vulkan::ImageMemoryBarrier(
                mVisibilityImage.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mRenderImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mVelocityImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mGradImage.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    RenderPass(cmd);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
        {
            Vulkan::ImageMemoryBarrier(
                mRenderImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                frame.resolvedRenderImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mVelocityImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    TaaResolvePass(cmd);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
        {
            Vulkan::ImageMemoryBarrier(
                frame.resolvedRenderImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
        }
    );

    FullscreenPass(cmd, imageIdx);

    if (!mImguiRenderer.UpdateVertexIndexBuffers(static_cast<u32>(mFrameIdx)))
    {
        return false;
    }

    if (mEnableUI)
    {
        VkRenderingAttachmentInfo renderingAttachmentInfo{};
        renderingAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        renderingAttachmentInfo.imageView = mSwapchain.images[imageIdx].view;
        renderingAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        renderingAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        renderingAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.extent = mSwapchain.extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &renderingAttachmentInfo;

        vkCmdBeginRendering(cmd, &renderingInfo);

        if (!mImguiRenderer.Render(cmd, u32(mFrameIdx)))
        {
            return false;
        }

        vkCmdEndRendering(cmd);
    }

    Vulkan::CmdImageMemoryBarrier(
        cmd,
        {
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE
            ),
        }
    );

    VK_CHECK(vkEndCommandBuffer(cmd));

    return true;
}

bool Renderer::CreateSwapchain()
{
    VK_CHECK(vkDeviceWaitIdle(mDevice));

    CleanupSwapchain();
    CleanupColorResources();
    CleanupDepthResources();

    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    VK_CHECK(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysicalDevice, mSurface, &surfaceCapabilities)
    );
    if (surfaceCapabilities.currentExtent.width != UINT32_MAX)
    {
        mSwapchain.extent = surfaceCapabilities.currentExtent;
    }
    else
    {
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSizeInPixels(mWindow, &width, &height))
        {
            SDL_PRINT_ERROR("SDL_GetWindowSizeInPixels");
            return false;
        }
        DEBUG_ASSERT(width > 0);
        DEBUG_ASSERT(height > 0);
        mSwapchain.extent.width = Clamp(
            u32(width),
            surfaceCapabilities.minImageExtent.width,
            surfaceCapabilities.maxImageExtent.width
        );
        mSwapchain.extent.height = Clamp(
            u32(height),
            surfaceCapabilities.minImageExtent.height,
            surfaceCapabilities.maxImageExtent.height
        );
    }

    mUniformData.swapchainWidth = mSwapchain.extent.width;
    mUniformData.swapchainHeight = mSwapchain.extent.height;
    mUniformData.viewToClip = Perspective(
        FOV_Y_RAD,
        f32(mSwapchain.extent.width) / f32(mSwapchain.extent.height),
        RENDERER_NEAR_PLANE
    );
    mUniformData.prevWorldToClip = mUniformData.worldToClip;
    mUniformData.worldToClip = mUniformData.viewToClip * mUniformData.worldToView;
    mUniformData.clipToWorld = Inverse(mUniformData.worldToClip);

    u32 surfaceFormatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mSurface, &surfaceFormatCount, nullptr);
    DEBUG_ASSERT(surfaceFormatCount > 0);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        mPhysicalDevice,
        mSurface,
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
            mSwapchain.surfaceFormat = surfaceFormats[i];
            swapchainSurfaceFormatFound = true;
            break;
        }
    }
    if (!swapchainSurfaceFormatFound)
    {
        fprintf(stderr, "vulkan: failed to find a suitable swapchain surface format\n");
        return false;
    }

    mSwapchain.minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    const u32 maxImageCount = surfaceCapabilities.maxImageCount;
    if (surfaceCapabilities.maxImageCount > 0 && maxImageCount < mSwapchain.minImageCount)
    {
        mSwapchain.minImageCount = maxImageCount;
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

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = mSurface;
    swapchainInfo.minImageCount = mSwapchain.minImageCount;
    swapchainInfo.imageFormat = mSwapchain.surfaceFormat.format;
    swapchainInfo.imageColorSpace = mSwapchain.surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = mSwapchain.extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = surfaceCapabilities.currentTransform;
    swapchainInfo.compositeAlpha = surfaceCompositeAlpha;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(mDevice, &swapchainInfo, nullptr, &mSwapchain.swapchain));

    u32 swapchainImageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(mDevice, mSwapchain.swapchain, &swapchainImageCount, nullptr));
    std::vector<VkImage> images(swapchainImageCount);
    VK_CHECK(
        vkGetSwapchainImagesKHR(mDevice, mSwapchain.swapchain, &swapchainImageCount, images.data())
    );

    mSwapchain.images.resize(swapchainImageCount);
    for (u32 i = 0; i < swapchainImageCount; ++i)
    {
        mSwapchain.images[i].image = images[i];
    }

    // Creating image views for every swapchain image.
    VkImageViewCreateInfo imageViewInfo{};
    imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewInfo.format = mSwapchain.surfaceFormat.format;
    imageViewInfo.subresourceRange = {};
    imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewInfo.subresourceRange.layerCount = 1;
    imageViewInfo.subresourceRange.levelCount = 1;

    for (u32 i = 0; i < swapchainImageCount; ++i)
    {
        imageViewInfo.image = mSwapchain.images[i].image;
        VkImageView imageView{};
        VK_CHECK(vkCreateImageView(mDevice, &imageViewInfo, nullptr, &imageView));
        mSwapchain.images[i].view = imageView;
    }

    if (!CreateColorResources())
    {
        return false;
    }

    if (!CreateDepthResources())
    {
        return false;
    }

    mSwapchainRecreated = true;

    return true;
}

void Renderer::CleanupSwapchain()
{
    for (size_t i = 0; i < mSwapchain.images.size(); ++i)
    {
        vkDestroyImageView(mDevice, mSwapchain.images[i].view, nullptr);
        mSwapchain.images[i].view = VK_NULL_HANDLE;
    }
    vkDestroySwapchainKHR(mDevice, mSwapchain.swapchain, nullptr);
    mSwapchain.swapchain = VK_NULL_HANDLE;
}

bool Renderer::CreateColorResources()
{
    VkFormat renderImageFormat{};
    if (!Vulkan::FindSupportedImageFormat(
            renderImageFormat,
            mPhysicalDevice,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            {
                VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
                VK_FORMAT_B10G11R11_UFLOAT_PACK32,
            }
        ))
    {
        fprintf(stderr, "vulkan: failed to find a suitable render image format\n");
        return false;
    }

    mRenderImageExtent.width = mSwapchain.extent.width * RENDER_SCALE;
    mRenderImageExtent.height = mSwapchain.extent.height * RENDER_SCALE;

    mUniformData.renderWidth = mRenderImageExtent.width;
    mUniformData.renderHeight = mRenderImageExtent.height;

    if (!Vulkan::CreateImage(
            mRenderImage,
            mDevice,
            mVmaAllocator,
            renderImageFormat,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            mRenderImageExtent.width,
            mRenderImageExtent.height,
            1,
            "RenderImage"
        ))
    {
        return false;
    }

    if (!Vulkan::FindSupportedImageFormat(
            mVisibilityImageFormat,
            mPhysicalDevice,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            {VK_FORMAT_R32G32_UINT}
        ))
    {
        fprintf(stderr, "vulkan: failed to find a suitable visibility buffer image format\n");
        return false;
    }

    if (!Vulkan::CreateImage(
            mVisibilityImage,
            mDevice,
            mVmaAllocator,
            mVisibilityImageFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            mRenderImageExtent.width,
            mRenderImageExtent.height,
            1,
            "VisibilityImage"
        ))
    {
        return false;
    }

    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (!Vulkan::CreateImage(
                mFrame[i].resolvedRenderImage,
                mDevice,
                mVmaAllocator,
                renderImageFormat,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                mRenderImageExtent.width,
                mRenderImageExtent.height,
                1,
                "ResolvedRenderImage"
            ))
        {
            return false;
        }
    }

    VkFormat velocityImageFormat{};
    if (!Vulkan::FindSupportedImageFormat(
            velocityImageFormat,
            mPhysicalDevice,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            {VK_FORMAT_R16G16_SFLOAT}
        ))
    {
        fprintf(stderr, "vulkan: failed to find a suitable velocity image format\n");
        return false;
    }

    if (!Vulkan::CreateImage(
            mVelocityImage,
            mDevice,
            mVmaAllocator,
            velocityImageFormat,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            mRenderImageExtent.width,
            mRenderImageExtent.height,
            1,
            "VelocityImage"
        ))
    {
        return false;
    }

    if (!Vulkan::FindSupportedImageFormat(
            mGradImageFormat,
            mPhysicalDevice,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            {VK_FORMAT_R32G32B32A32_SFLOAT}
        ))
    {
        fprintf(stderr, "vulkan: failed to find a suitable grad image format\n");
        return false;
    }

    // TODO: wrapper that takes std::initializer_list of formats.
    if (!Vulkan::CreateImage(
            mGradImage,
            mDevice,
            mVmaAllocator,
            mGradImageFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            mRenderImageExtent.width,
            mRenderImageExtent.height,
            1,
            "GradImage"
        ))
    {
        return false;
    }

    return true;
}

void Renderer::CleanupColorResources()
{
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        Vulkan::DestroyImage(mFrame[i].resolvedRenderImage, mDevice, mVmaAllocator);
    }
    Vulkan::DestroyImage(mGradImage, mDevice, mVmaAllocator);
    Vulkan::DestroyImage(mVisibilityImage, mDevice, mVmaAllocator);
    Vulkan::DestroyImage(mRenderImage, mDevice, mVmaAllocator);
    Vulkan::DestroyImage(mVelocityImage, mDevice, mVmaAllocator);
}

bool Renderer::CreateDepthResources()
{
    if (!Vulkan::FindSupportedImageFormat(
            mDepthFormat,
            mPhysicalDevice,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            {VK_FORMAT_D32_SFLOAT}
        ))
    {
        fprintf(stderr, "vulkan: failed to find a suitable depth format\n");
        return false;
    }

    if (!Vulkan::CreateImage(
            mDepthImage,
            mDevice,
            mVmaAllocator,
            mDepthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            mRenderImageExtent.width,
            mRenderImageExtent.height,
            1,
            "DepthImage"
        ))
    {
        return false;
    }

    VkFormat depthPyramidFormat{};
    if (!Vulkan::FindSupportedImageFormat(
            depthPyramidFormat,
            mPhysicalDevice,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            {VK_FORMAT_R32G32_SFLOAT}
        ))
    {
        fprintf(stderr, "vulkan: failed to find a suitable depth pyramid format\n");
        return false;
    }

    return true;
}

void Renderer::CleanupDepthResources()
{
    Vulkan::DestroyImage(mDepthImage, mDevice, mVmaAllocator);
}
