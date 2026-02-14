#include "Renderer.hpp"

#include "Utils.hpp"
#include "Math/Mat4.hpp"

#include <stdio.h>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#define SDL_PRINT_ERROR(functionName) \
    fprintf(stderr, "%s:%d: " functionName " failed: %s\n", __FILE__, __LINE__, SDL_GetError())

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
                "cleanup (glfw), also RenderDoc doesn't work on Wayland. It seems that using X11 "
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
        // DEFER(SDL_Quit());

        VK_CHECK(volkInitialize());

        mWindow = SDL_CreateWindow(
            "vulkan",
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
        // DEFER(SDL_DestroyWindow(window));

        (void)SDL_SetWindowPosition(mWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        (void)SDL_SetWindowRelativeMouseMode(mWindow, true);
    }

    // Instance.
    {
        u32 vulkanApiVersion = 0;
        VK_CHECK(vkEnumerateInstanceVersion(&vulkanApiVersion));
        if (vulkanApiVersion < VK_API_VERSION_1_3)
        {
            fprintf(stderr, "vulkan: API version 1.3 is required\n");
            return false;
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "None";
        appInfo.applicationVersion = 1;
        appInfo.pEngineName = "None";
        appInfo.engineVersion = 1;
        appInfo.apiVersion = VK_API_VERSION_1_3;

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

        volkLoadInstance(mInstance);
    }

    // Surface.
    if (!SDL_Vulkan_CreateSurface(mWindow, mInstance, nullptr, &mSurface))
    {
        SDL_PRINT_ERROR("SDL_Vulkan_CreateSurface ");
        return false;
    }

    // Physical device.
    // clang-format off
    const char* const requiredDeviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    };
    // clang-format on
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

            VkPhysicalDeviceSubgroupProperties subgroupProperties;
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
            VkPhysicalDeviceVulkan13Features vulkanFeatures13{};
            vulkanFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

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

            bool supportsRequiredFeatures = true;
            supportsRequiredFeatures
                &= physicalDeviceFeatures.features.vertexPipelineStoresAndAtomics;
            supportsRequiredFeatures &= vulkanFeatures13.dynamicRendering;
            supportsRequiredFeatures &= vulkanFeatures13.synchronization2;
            supportsRequiredFeatures &= vulkanFeatures12.scalarBlockLayout;
            supportsRequiredFeatures &= vulkanFeatures12.shaderInt8;
            supportsRequiredFeatures &= vulkanFeatures12.storageBuffer8BitAccess;
            supportsRequiredFeatures &= vulkanFeatures12.uniformAndStorageBuffer8BitAccess;
            supportsRequiredFeatures &= physicalDeviceFeatures.features.multiDrawIndirect;
            supportsRequiredFeatures &= physicalDeviceFeatures.features.fragmentStoresAndAtomics;

            bool deviceOk = true;
            deviceOk &= supportsVulkan13;
            deviceOk &= supportsGraphicsAndPresentation;
            deviceOk &= supportsRequiredExtensions;
            deviceOk &= supportsRequiredFeatures;
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

        VkPhysicalDeviceVulkan13Features vulkanFeatures13{};
        vulkanFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkanFeatures13.dynamicRendering = VK_TRUE;
        vulkanFeatures13.synchronization2 = VK_TRUE;

        VkPhysicalDeviceVulkan12Features vulkanFeatures12{};
        vulkanFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vulkanFeatures12.pNext = &vulkanFeatures13;
        vulkanFeatures12.scalarBlockLayout = VK_TRUE;
        vulkanFeatures12.shaderInt8 = VK_TRUE;
        vulkanFeatures12.storageBuffer8BitAccess = VK_TRUE;
        vulkanFeatures12.uniformAndStorageBuffer8BitAccess = VK_TRUE;

        VkPhysicalDeviceVulkan11Features vulkanFeatures11{};
        vulkanFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        vulkanFeatures11.pNext = &vulkanFeatures12;
        vulkanFeatures11.shaderDrawParameters = VK_TRUE;

        VkPhysicalDeviceFeatures2 physicalDeviceFeatures{};
        physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        physicalDeviceFeatures.pNext = &vulkanFeatures11;
        physicalDeviceFeatures.features.multiDrawIndirect = VK_TRUE;
        physicalDeviceFeatures.features.shaderInt64 = VK_TRUE;
        physicalDeviceFeatures.features.vertexPipelineStoresAndAtomics = VK_TRUE;
        physicalDeviceFeatures.features.fragmentStoresAndAtomics = VK_TRUE;

        constexpr f32 queuePriority = 1.0f;

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
        vkGetDeviceQueue(mDevice, queueInfo.familyIdx, queueInfo.queueIdx, &queueInfo.queue);
        mQueueInfo = queueInfo;

        volkLoadDevice(mDevice);
    }

    if (!RecreateSwapchain())
    {
        return false;
    }

    // Uniform buffer.
    {
        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            // NOTE: creating a host visible, coherent, device local buffer.
            // Should be legal even on discrete GPUs if total allocated
            // size is less than 200 MB or so.
            const bool result = Vulkan::CreateBuffer(
                mFrame[i].uniformBuffer,
                mPhysicalDevice,
                mDevice,
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
    }

    // Particle system buffers.
    {
        bool result = Vulkan::CreateBuffer(
            mParticleCountBuffer,
            mPhysicalDevice,
            mDevice,
            sizeof(ParticleCountData),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "ParticleCountBuffer"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mParticleCountUniformBuffer,
            mPhysicalDevice,
            mDevice,
            sizeof(ParticleCountData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "ParticleCountUniformBuffer"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mVelocityMassInvBuffer,
            mPhysicalDevice,
            mDevice,
            sizeof(Vec4) * MAX_PARTICLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "VelocityMassInvBuffer"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mPositionDistToForceFieldInvBuffer,
            mPhysicalDevice,
            mDevice,
            sizeof(Vec4) * MAX_PARTICLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "PositionDistToForceFieldInvBuffer"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mRadiusBuffer,
            mPhysicalDevice,
            mDevice,
            sizeof(f32) * MAX_PARTICLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "RadiusBuffer"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mTimeLeftBuffer,
            mPhysicalDevice,
            mDevice,
            sizeof(f32) * MAX_PARTICLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "TimeLeftBuffer"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mDeadBuffer,
            mPhysicalDevice,
            mDevice,
            sizeof(u32) * MAX_PARTICLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "DeadBuffer"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mAliveBuffer1,
            mPhysicalDevice,
            mDevice,
            sizeof(u32) * MAX_PARTICLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "AliveBuffer1"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mAliveBuffer2,
            mPhysicalDevice,
            mDevice,
            sizeof(u32) * MAX_PARTICLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "AliveBuffer2"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mEmitterIdxBuffer,
            mPhysicalDevice,
            mDevice,
            sizeof(u8) * MAX_PARTICLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "EmitterIdxBuffer"
        );
        if (!result)
        {
            return false;
        }

        // NOTE: creating a host visible, coherent, device local buffer.
        // Should be legal even on discrete GPUs if total allocated
        // size is less than 200 MB or so.
        result = Vulkan::CreateBuffer(
            mEmitterBuffer,
            mPhysicalDevice,
            mDevice,
            sizeof(Emitter) * MAX_PARTICLES,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            "EmitterBuffer"
        );
        if (!result)
        {
            return false;
        }

        result = Vulkan::CreateBuffer(
            mIndirectBuffer,
            mPhysicalDevice,
            mDevice,
            sizeof(ParticleIndirectData),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "IndirectBuffer"
        );
        if (!result)
        {
            return false;
        }
    }

    // Descriptor binding descriptions.
    // TODO: SPIR-V reflection. Although for a small demo it's not that cumbersome.
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        Frame& frame = mFrame[i];

        // clang-format off
        Vulkan::DescriptorBindingInfo bindingInfos[] =
        {
            {
                "uniformBuffer",
                UNIFORM_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                {
                    frame.uniformBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "particleCountBuffer",
                PARTICLE_COUNT_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT,
                {
                    mParticleCountBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "particleCountUniformBuffer",
                PARTICLE_COUNT_UNIFORM_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT,
                {
                    mParticleCountUniformBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "indirectBuffer",
                INDIRECT_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT,
                {
                    mIndirectBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "velocityMassInvBuffer",
                VELOCITY_MASS_INV_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT,
                {
                    mVelocityMassInvBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "positionDistToForceFieldInvBuffer",
                POSITION_DIST_TO_FORCE_FIELD_INV_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                {
                    mPositionDistToForceFieldInvBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "radiusBuffer",
                RADIUS_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                {
                    mRadiusBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "timeLeftBuffer",
                TIME_LEFT_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                {
                    mTimeLeftBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "deadBuffer",
                DEAD_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT,
                {
                    mDeadBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "aliveBuffer1",
                ALIVE_BUFFER_1_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                {
                    mAliveBuffer1.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "aliveBuffer2",
                ALIVE_BUFFER_2_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                {
                    mAliveBuffer2.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "emitterIdxBuffer",
                EMITTER_IDX_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                {
                    mEmitterIdxBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "emitterBuffer",
                EMITTER_BUFFER_BINDING,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                {
                    mEmitterBuffer.buffer,
                    0,
                    VK_WHOLE_SIZE
                }
            },
            {
                "renderImageSampler",
                RENDER_IMAGE_SAMPLER_BINDING,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                {
                    mRenderImage.sampler,
                    mRenderImage.view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            },
        };
        static_assert(sizeof(bindingInfos) == sizeof(frame.descriptorBindingInfos));
        memcpy(frame.descriptorBindingInfos, bindingInfos, sizeof(bindingInfos));
    }
    // clang-format on

    // Graphics and compute pipelines.
    {
        VkShaderModule shaderModule{};
        if (!Vulkan::CreateShaderModule(shaderModule, mDevice, "Renderer.slang.spv"))
        {
            return false;
        }
        DEFER(vkDestroyShaderModule(mDevice, shaderModule, nullptr));

        VkShaderModule computeShaderModule{};
        if (!Vulkan::CreateShaderModule(computeShaderModule, mDevice, "ParticleSystem.slang.spv"))
        {
            return false;
        }
        DEFER(vkDestroyShaderModule(mDevice, computeShaderModule, nullptr));

        VkPipelineShaderStageCreateInfo shaderStagesInfo[2]{};

        shaderStagesInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStagesInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStagesInfo[0].module = shaderModule;
        shaderStagesInfo[0].pName = "ParticleVertexMain";

        shaderStagesInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStagesInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStagesInfo[1].module = shaderModule;
        shaderStagesInfo[1].pName = "ParticleFragmentMain";

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

        VkPipelineViewportStateCreateInfo viewportInfo{};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.viewportCount = 1;
        viewportInfo.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
        rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationInfo.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampleInfo{};
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // clang-format off
        constexpr VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        // clang-format on

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = u32(ARRAY_SIZE(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        const u32 descriptorBindingCount = u32(ARRAY_SIZE(mFrame[0].descriptorBindingInfos));
        std::vector<VkDescriptorSetLayoutBinding> setBindings;
        setBindings.reserve(descriptorBindingCount);
        for (u32 i = 0; i < descriptorBindingCount; ++i)
        {
            const Vulkan::DescriptorBindingInfo& b = mFrame[0].descriptorBindingInfos[i];
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = b.binding;
            binding.descriptorType = b.descriptorType;
            binding.descriptorCount = 1;
            binding.stageFlags = b.stageFlags;
            setBindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        setInfo.bindingCount = descriptorBindingCount;
        setInfo.pBindings = setBindings.data();

        VK_CHECK(vkCreateDescriptorSetLayout(
            mDevice,
            &setInfo,
            nullptr,
            &mRenderPipeline.descriptorSetLayout
        ));

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &mRenderPipeline.descriptorSetLayout;

        VK_CHECK(vkCreatePipelineLayout(mDevice, &layoutInfo, nullptr, &mRenderPipeline.layout));

        VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
        pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingInfo.colorAttachmentCount = 1;
        pipelineRenderingInfo.pColorAttachmentFormats = &mSwapchain.surfaceFormat.format;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &pipelineRenderingInfo;
        pipelineInfo.stageCount = ARRAY_SIZE(shaderStagesInfo);
        pipelineInfo.pStages = shaderStagesInfo;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineInfo.pViewportState = &viewportInfo;
        pipelineInfo.pRasterizationState = &rasterizationInfo;
        pipelineInfo.pMultisampleState = &multisampleInfo;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = mRenderPipeline.layout;

        VK_CHECK(vkCreateGraphicsPipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            nullptr,
            &mRenderPipeline.pipeline
        ));

        // Fullscreen triangle graphics pipeline.
        shaderStagesInfo[0].pName = "FullscreenVertexMain";
        shaderStagesInfo[1].pName = "FullscreenFragmentMain";

        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VK_CHECK(vkCreateGraphicsPipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            nullptr,
            &mFullscreenPipeline.pipeline
        ));

        // Particle system compute pipelines.
        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = computeShaderModule;
        shaderStageInfo.pName = "SimulationMain";

        mSimulationPipeline.descriptorSetLayout = mRenderPipeline.descriptorSetLayout;
        mSimulationPipeline.layout = mRenderPipeline.layout;

        VkComputePipelineCreateInfo computePipelineInfo{};
        computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineInfo.stage = shaderStageInfo;
        computePipelineInfo.layout = mSimulationPipeline.layout;

        vkCreateComputePipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &computePipelineInfo,
            nullptr,
            &mSimulationPipeline.pipeline
        );

        computePipelineInfo.stage.pName = "InitMain";
        mInitPipeline.descriptorSetLayout = mRenderPipeline.descriptorSetLayout;
        mInitPipeline.layout = mRenderPipeline.layout;
        vkCreateComputePipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &computePipelineInfo,
            nullptr,
            &mInitPipeline.pipeline
        );

        computePipelineInfo.stage.pName = "EmitterMain";
        mEmitterPipeline.descriptorSetLayout = mRenderPipeline.descriptorSetLayout;
        mEmitterPipeline.layout = mRenderPipeline.layout;
        vkCreateComputePipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &computePipelineInfo,
            nullptr,
            &mEmitterPipeline.pipeline
        );

        computePipelineInfo.stage.pName = "BeforeSimulationMain";
        mBeforeSimulationPipeline.descriptorSetLayout = mRenderPipeline.descriptorSetLayout;
        mBeforeSimulationPipeline.layout = mRenderPipeline.layout;
        vkCreateComputePipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &computePipelineInfo,
            nullptr,
            &mBeforeSimulationPipeline.pipeline
        );

        computePipelineInfo.stage.pName = "AfterSimulationMain";
        mAfterSimulationPipeline.descriptorSetLayout = mRenderPipeline.descriptorSetLayout;
        mAfterSimulationPipeline.layout = mRenderPipeline.layout;
        vkCreateComputePipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &computePipelineInfo,
            nullptr,
            &mAfterSimulationPipeline.pipeline
        );
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

    // Write descriptor sets.
    {
        for (Frame& frame : mFrame)
        {
            for (size_t i = 0; i < ARRAY_SIZE(frame.descriptorBindingInfos); ++i)
            {
                const Vulkan::DescriptorBindingInfo& b = frame.descriptorBindingInfos[i];
                VkWriteDescriptorSet set{};
                set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                set.dstSet = 0;
                set.dstBinding = b.binding;
                set.descriptorCount = 1;
                set.descriptorType = b.descriptorType;
                switch (b.descriptorType)
                {
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    set.pBufferInfo = &b.resourceInfo.buffer;
                    break;

                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    set.pImageInfo = &b.resourceInfo.image;
                    break;

                default:
                    fprintf(stderr, "vulkan: unhandled descriptor type %d\n", b.descriptorType);
                    return false;
                }
                frame.writeDescriptorSets[i] = set;
            }
        }
    }

    // Particle system data.
    {
#if defined(PARTICLE_SYSTEM_SINGLE_PARTICLE)
        Emitter& e = mEmitters[0];
        e = {};

        e.life = 2.0f;
        DEBUG_ASSERT(e.life > 0.0f);
        e.lifeInv = 1.0f / e.life;
        e.positionInterval = 0.0f;
        e.color = Vec3{2.0f, 0.0f, 0.0f};
        e.radiusMin = 5.0f;
        e.radius = 0.0f;
        e.initialVelocity = Vec3{0.0f};
        e.initialVelocityInterval = 0.0f;
        e.massInv = 1.0f;

        mUniformData.emitterCount = 1;
        mUniformData.emitterPeriod = 3.0f;
        mUniformData.emitterTimeAccum = mUniformData.emitterPeriod;

        mUniformData.forceField = {Vec3{0.0f, 50.0f, 0.0f}, 0.0f, 1.0f};

#elif defined(PARTICLE_SYSTEM_STRESS_TEST)
        const size_t maxEmitters = ARRAY_SIZE(mEmitters);
        static_assert(maxEmitters % 8 == 0);
        const size_t maxX = 8;
        const size_t maxZ = maxEmitters / 8;
        const f32 gridSize = 2.0f;

        u32 rng = 0x1337;
        std::vector<Vec3> positions;
        std::vector<f32> lifes;
        positions.reserve(maxEmitters);
        lifes.reserve(maxEmitters);
        for (size_t x = 0; x < maxX; ++x)
        {
            for (size_t z = 0; z < maxZ; ++z)
            {
                positions.push_back({gridSize * f32(x), 0.0f, gridSize * f32(z)});
                // lifes.push_back(0.8f * f32(x) / f32(maxX));
                lifes.push_back(LfsrNextGetFloatAbs(rng, 0.8f));
            }
        }
        ASSERT(positions.size() == maxEmitters);

        for (size_t i = 0; i < maxEmitters; ++i)
        {
            Emitter& e = mEmitters[i];
            e.position = positions[i];
            // e.life = 2.2f + f32(maxEmitters) / f32(i) * 0.1f;
            // e.life = 2.3f;
            e.life = 2.0f + lifes[i];
            DEBUG_ASSERT(e.life > 0.0f);
            e.lifeInv = 1.0f / e.life;
            e.positionInterval = gridSize / 2.0f;
            e.color = Vec3{
                LfsrNextGetFloatAbs(rng, 0.1f),
                LfsrNextGetFloatAbs(rng, 0.1f),
                LfsrNextGetFloatAbs(rng, 0.1f)
            };
            e.radiusMin = 0.05f;
            e.radius = 0.1f;
            e.initialVelocity = {0.0f, 1.0f, 0.0f};
            e.initialVelocityInterval = 0.2f;
            e.massInv = 1.0f;
        }
        mUniformData.emitterCount = u32(maxEmitters);

        mUniformData.emitterPeriod = 0.0003f;

        mUniformData.forceField = {Vec3{0.0f, 50.0f, 0.0f}, 0.0f, 0.0f};

#elif defined(PARTICLE_SYSTEM_DEMO)
        const float positionOffset = 10.0f;
        const Vec3 positions[] = {
            {-positionOffset, 0.0f, 0.0f},
            {positionOffset, 0.0f, 0.0f},
            {0.0f, -positionOffset, 0.0f},
            {0.0f, positionOffset, 0.0f},
            {0.0f, 0.0f, -positionOffset},
            {0.0f, 0.0f, positionOffset},
        };
        ASSERT(ARRAY_SIZE(positions) < ARRAY_SIZE(mEmitters));
        for (size_t i = 0; i < ARRAY_SIZE(positions); ++i)
        {
            mEmitters[i].position = positions[i];
        }
        mUniformData.emitterCount = u32(ARRAY_SIZE(positions));

        for (u32 i = 0; i < mUniformData.emitterCount; ++i)
        {
            Emitter& e = mEmitters[i];
            e.position = positions[i];
            e.life = 10.0f;
            DEBUG_ASSERT(e.life > 0.0f);
            e.lifeInv = 1.0f / e.life;
            e.positionInterval = 0.2f;
            e.color = Vec3{0.3f, 0.8f, 0.9f};
            e.radiusMin = 0.1f;
            e.radius = 0.2f;
            e.initialVelocity = -Normalize(e.position) * 1.0f;
            e.initialVelocityInterval = 0.125;
            e.massInv = 1.0f;
        }

        mUniformData.emitterPeriod = 0.007f;

        mUniformData.forceField = {Vec3{0.0f, 0.0f, 0.0f}, 3.0f, 10.0f};
#else
#error "define 1 mode for particle system"
#endif

        DEBUG_ASSERT(mUniformData.emitterPeriod > 0.0f);
        DEBUG_ASSERT(mUniformData.emitterCount > 0);

        memcpy(mEmitterBuffer.mapped, mEmitters, sizeof(mEmitters));
    }

    ResetParticleSystem();

    return true;
}

void Renderer::Cleanup()
{
    (void)vkDeviceWaitIdle(mDevice);

    CleanupColorResources();

    Vulkan::DestroyBuffer(mDevice, mTimeLeftBuffer);
    Vulkan::DestroyBuffer(mDevice, mRadiusBuffer);
    Vulkan::DestroyBuffer(mDevice, mPositionDistToForceFieldInvBuffer);
    Vulkan::DestroyBuffer(mDevice, mVelocityMassInvBuffer);
    Vulkan::DestroyBuffer(mDevice, mEmitterBuffer);
    Vulkan::DestroyBuffer(mDevice, mEmitterIdxBuffer);
    Vulkan::DestroyBuffer(mDevice, mIndirectBuffer);
    Vulkan::DestroyBuffer(mDevice, mAliveBuffer2);
    Vulkan::DestroyBuffer(mDevice, mAliveBuffer1);
    Vulkan::DestroyBuffer(mDevice, mDeadBuffer);
    Vulkan::DestroyBuffer(mDevice, mParticleCountUniformBuffer);
    Vulkan::DestroyBuffer(mDevice, mParticleCountBuffer);
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        Vulkan::DestroyBuffer(mDevice, mFrame[i].uniformBuffer);
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
    vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
    vkDestroyPipeline(mDevice, mEmitterPipeline.pipeline, nullptr);
    vkDestroyPipeline(mDevice, mAfterSimulationPipeline.pipeline, nullptr);
    vkDestroyPipeline(mDevice, mBeforeSimulationPipeline.pipeline, nullptr);
    vkDestroyPipeline(mDevice, mInitPipeline.pipeline, nullptr);
    vkDestroyPipeline(mDevice, mSimulationPipeline.pipeline, nullptr);
    vkDestroyDescriptorSetLayout(mDevice, mRenderPipeline.descriptorSetLayout, nullptr);
    vkDestroyPipeline(mDevice, mFullscreenPipeline.pipeline, nullptr);
    vkDestroyPipeline(mDevice, mRenderPipeline.pipeline, nullptr);
    vkDestroyPipelineLayout(mDevice, mRenderPipeline.layout, nullptr);
    CleanupSwapchain();
    vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
    vkDestroyDevice(mDevice, nullptr);
    vkDestroyInstance(mInstance, nullptr);
}

bool Renderer::StartNewFrame()
{
    DEBUG_ASSERT(!mNewFrameStarted);

    Frame& frame = mFrame[mFrameIdx];

    VK_CHECK(vkWaitForFences(mDevice, 1, &frame.queueSubmitFence, VK_TRUE, 1'000'000'000));
    VK_CHECK(vkResetFences(mDevice, 1, &frame.queueSubmitFence));

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
        if (!RecreateSwapchain())
        {
            return false;
        }
    }

    ++mUniformData.frameCount;
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
    mUniformData.emitterTimeAccum += deltaTime;
    mUniformData.particleSimTimeAccum += deltaTime;

    if (!RecordCommandBuffer(imageIdx))
    {
        return false;
    }

    memcpy(frame.uniformBuffer.mapped, &mUniformData, sizeof(mUniformData));

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
    // NOTE: citing the spec (chapter "Availability, Visibility, and Domain Operations"):
    //     vkQueueSubmit performs a memory domain operation from host to device,
    //     and a visibility operation with source scope of the device domain and
    //     destination scope of all agents and references on the device.
    //
    // Operations are described in "7.1. Execution and Memory Dependencies".
    // According to the spec, it should be safe to assume that all prior buffer
    // writes from host are both available and visible and no additional
    // synchronization is required. However, it's illegal to start writing new
    // data to buffers while they are still being used by device, therefore a
    // fence is needed at the beginning of the frame.
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

    mFrameIdx = (mFrameIdx + 1) % RENDERER_MAX_FRAMES_IN_FLIGHT;

    return true;
}

void Renderer::UpdateCamera(const Mat4& worldToView)
{
    mUniformData.worldToView = worldToView;
    mUniformData.worldToClip = mUniformData.viewToClip * worldToView;
}

void Renderer::PauseRendering(bool paused)
{
    mRenderingPaused = paused;
}

void Renderer::ResetParticleSystem()
{
    mNeedResetParticleSystem = true;
}

bool Renderer::RecordCommandBuffer(u32 imageIdx)
{
    Frame& frame = mFrame[mFrameIdx];
    const VkCommandBuffer cmd = frame.commandBuffer;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo{};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    vkCmdPushDescriptorSetKHR(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        mSimulationPipeline.layout,
        0,
        u32(ARRAY_SIZE(frame.writeDescriptorSets)),
        frame.writeDescriptorSets
    );

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    if (mNeedResetParticleSystem)
    {
        mNeedResetParticleSystem = false;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mInitPipeline.pipeline);
        vkCmdDispatch(
            cmd,
            (MAX_PARTICLES + PARTICLES_INIT_WORKGROUP_SIZE - 1) / PARTICLES_INIT_WORKGROUP_SIZE,
            1,
            1
        );
        Vulkan::CmdMemoryBarrier(
            cmd,
            {
                Vulkan::MemoryBarrier(
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                ),
            }
        );
    }

    const u32 emitCount = u32(floorf(mUniformData.emitterTimeAccum / mUniformData.emitterPeriod));
    mUniformData.emitterTimeAccum -= mUniformData.emitterPeriod * f32(emitCount);
    mUniformData.emitterIterations = emitCount;

    if (mUniformData.emitterIterations > 0)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mEmitterPipeline.pipeline);
        vkCmdDispatch(
            cmd,
            (mUniformData.emitterCount + PARTICLE_EMITTER_WORKGROUP_SIZE - 1)
                / PARTICLE_EMITTER_WORKGROUP_SIZE,
            1,
            1
        );
    }

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    const u32 simulationCount
        = u32(floorf(mUniformData.particleSimTimeAccum / PARTICLE_SIM_TIME_STEP));
    mUniformData.particleSimTimeAccum -= PARTICLE_SIM_TIME_STEP * f32(simulationCount);
    mUniformData.particleSimIterations = simulationCount;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mBeforeSimulationPipeline.pipeline);
    vkCmdDispatch(cmd, 1, 1, 1);

    Vulkan::CmdBufferMemoryBarrier(
        cmd,
        {
            Vulkan::BufferMemoryBarrier(
                mParticleCountBuffer.buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT
            ),
        }
    );

    VkBufferCopy region{};
    region.size = sizeof(ParticleCountData);
    vkCmdCopyBuffer(
        cmd,
        mParticleCountBuffer.buffer,
        mParticleCountUniformBuffer.buffer,
        1,
        &region
    );

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
            ),
        }
    );

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mSimulationPipeline.pipeline);
    vkCmdDispatchIndirect(cmd, mIndirectBuffer.buffer, 0);

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            ),
        }
    );

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mAfterSimulationPipeline.pipeline);
    vkCmdDispatch(cmd, 1, 1, 1);

    // NOTE: specifying VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT as
    // srcStageMask forms dependency chain with vkQueueSubmit, since
    // waitDstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    // therefore, transitioning image layout will occur only after
    // imageAcquireSemaphore is signalled.
    Vulkan::CmdBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mRenderImage.image,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
        }
    );

    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        Swap(
            mFrame[i].writeDescriptorSets[ALIVE_BUFFER_1_BINDING].dstBinding,
            mFrame[i].writeDescriptorSets[ALIVE_BUFFER_2_BINDING].dstBinding
        );
    }

    vkCmdPushDescriptorSetKHR(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        mRenderPipeline.layout,
        0,
        u32(ARRAY_SIZE(frame.writeDescriptorSets)),
        frame.writeDescriptorSets
    );

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderPipeline.pipeline);

    VkRenderingAttachmentInfo renderingAttachmentInfo{};
    renderingAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    renderingAttachmentInfo.imageView = mRenderImage.view;
    renderingAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    renderingAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    renderingAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    renderingAttachmentInfo.clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

    VkRect2D renderArea{};
    renderArea.offset.x = 0;
    renderArea.offset.y = 0;
    renderArea.extent = mRenderImageExtent;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = renderArea;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &renderingAttachmentInfo;

    vkCmdBeginRendering(cmd, &renderingInfo);

#if 0
    // NOTE: flipping Y here, allowed since VK_KHR_MAINTENANCE_1 (core in 1.1).
    VkViewport viewport{};
    viewport.width = f32(mSwapchain.extent.width);
    viewport.height = -f32(mSwapchain.extent.height);
    viewport.y = f32(mSwapchain.extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
#else
    VkViewport viewport{};
    viewport.width = f32(mRenderImageExtent.width);
    viewport.height = f32(mRenderImageExtent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
#endif

    VkRect2D scissor{};
    scissor.extent = mRenderImageExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDrawIndirect(cmd, mIndirectBuffer.buffer, offsetof(ParticleIndirectData, drawCmd), 1, 0);

    vkCmdEndRendering(cmd);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
        {
            Vulkan::ImageMemoryBarrier(
                mRenderImage.image,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    viewport.width = f32(mSwapchain.extent.width);
    viewport.height = f32(mSwapchain.extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    scissor.extent = mSwapchain.extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    renderingAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    renderingAttachmentInfo.imageView = mSwapchain.images[imageIdx].view;
    renderingAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    renderingAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    renderingAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    renderingAttachmentInfo.clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

    renderArea.offset.x = 0;
    renderArea.offset.y = 0;
    renderArea.extent = mSwapchain.extent;

    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = renderArea;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &renderingAttachmentInfo;

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mFullscreenPipeline.pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
        {
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_ASPECT_COLOR_BIT,
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

bool Renderer::RecreateSwapchain()
{
    VK_CHECK(vkDeviceWaitIdle(mDevice));

    CleanupSwapchain();
    CleanupColorResources();

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
        NEAR_PLANE
    );
    mUniformData.worldToClip = mUniformData.viewToClip * mUniformData.worldToView;

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
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
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

    return true;
}

void Renderer::CleanupSwapchain()
{
    for (size_t i = 0, size = mSwapchain.images.size(); i < size; ++i)
    {
        vkDestroyImageView(mDevice, mSwapchain.images[i].view, nullptr);
        mSwapchain.images[i].view = VK_NULL_HANDLE;
    }
    vkDestroySwapchainKHR(mDevice, mSwapchain.swapchain, nullptr);
    mSwapchain.swapchain = VK_NULL_HANDLE;
}

bool Renderer::CreateColorResources()
{
    mRenderImageExtent.width = u32(f32(mSwapchain.extent.width) * PARTICLE_SIM_RENDER_SCALE);
    mRenderImageExtent.height = u32(f32(mSwapchain.extent.height) * PARTICLE_SIM_RENDER_SCALE);

    VkExtent3D extent{};
    extent.width = mRenderImageExtent.width;
    extent.height = mRenderImageExtent.height;
    extent.depth = 1;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = mSwapchain.surfaceFormat.format;
    imageInfo.extent = extent;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(mDevice, &imageInfo, nullptr, &mRenderImage.image));

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(mDevice, mRenderImage.image, &memoryRequirements);

    u32 memoryTypeIdx = 0;
    const bool result = Vulkan::FindMemoryType(
        memoryTypeIdx,
        mPhysicalDevice,
        memoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    if (!result)
    {
        fprintf(stderr, "vulkan: failed to find a suitable memory type\n");
        return false;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = memoryTypeIdx;
    VK_CHECK(vkAllocateMemory(mDevice, &allocateInfo, nullptr, &mRenderImage.memory));

    VK_CHECK(vkBindImageMemory(mDevice, mRenderImage.image, mRenderImage.memory, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.image = mRenderImage.image;
    viewInfo.format = mSwapchain.surfaceFormat.format;
    viewInfo.subresourceRange = {};
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.subresourceRange.levelCount = 1;
    VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mRenderImage.view));

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    VK_CHECK(vkCreateSampler(mDevice, &samplerInfo, nullptr, &mRenderImage.sampler));

    if (!Vulkan::DebugNameObject(
            mDevice,
            VK_OBJECT_TYPE_IMAGE,
            reinterpret_cast<u64>(mRenderImage.image),
            "RenderImage"
        ))
    {
        return false;
    }

    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        Frame& frame = mFrame[i];
        Vulkan::DescriptorResourceInfo& info
            = frame.descriptorBindingInfos[RENDER_IMAGE_SAMPLER_BINDING].resourceInfo;
        info.image.imageView = mRenderImage.view;
        info.image.sampler = mRenderImage.sampler;
    }

    return true;
}

void Renderer::CleanupColorResources()
{
    vkDestroyImageView(mDevice, mRenderImage.view, nullptr);
    mRenderImage.view = VK_NULL_HANDLE;
    vkFreeMemory(mDevice, mRenderImage.memory, nullptr);
    mRenderImage.memory = VK_NULL_HANDLE;
    vkDestroyImage(mDevice, mRenderImage.image, nullptr);
    mRenderImage.image = VK_NULL_HANDLE;
    vkDestroySampler(mDevice, mRenderImage.sampler, nullptr);
    mRenderImage.sampler = VK_NULL_HANDLE;
}
