#include "Renderer.hpp"

#include "Vulkan.hpp"
#include "Meshes.hpp"
#include "../TimeMeter.hpp"
#include "../PackUtils.hpp"
#include "../Math/Utils.hpp"
#include "../Math/Mat3.hpp"
#include "../Math/Mat4.hpp"
#include "../Arena.hpp"
#include "../Utils.hpp"

#include "SDL3/SDL_vulkan.h"
#include "SDL3/SDL_timer.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"

// GPU gems 2, Chapter 17, Efficient Soft-Edged Shadows Using Pixel Shader Branching, Yury Uralsky
static Slice<i8> CreateShadowJitterOffsets(int size, int samplesU, int samplesV, Arena& arena)
{
    u32 randomU32 = 1337;

    Slice<i8> result{};
    result.mCount = size * size * samplesU * samplesV * 4 / 2;
    result.mData = arena.AllocOrDie<i8>(result.mCount);

    const int gridSize = samplesU * samplesV / 2;

    for (int i = 0; i < size; ++i)
    {
        for (int j = 0; j < size; ++j)
        {
            for (int k = 0; k < gridSize; ++k)
            {
                const int x = k % (samplesU / 2);
                const int y = (samplesV - 1) - k / (samplesU / 2);

                // Generate points on a regular rectangular grid size samplesU * samplesV.
                Vec4 gridPoints;
                gridPoints[0] = (static_cast<f32>(x) * 2.0f + 0.5f) / static_cast<f32>(samplesU);
                gridPoints[1] = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(samplesV);
                gridPoints[2]
                    = (static_cast<f32>(x) * 2.0f + 1.0f + 0.5f) / static_cast<f32>(samplesU);
                gridPoints[3] = gridPoints[1];

                // Jitter position.
                gridPoints[0] += LfsrNextGetFloat(randomU32, 0.5f / static_cast<f32>(samplesU));
                gridPoints[1] += LfsrNextGetFloat(randomU32, 0.5f / static_cast<f32>(samplesV));
                gridPoints[2] += LfsrNextGetFloat(randomU32, 0.5f / static_cast<f32>(samplesU));
                gridPoints[3] += LfsrNextGetFloat(randomU32, 0.5f / static_cast<f32>(samplesV));

                // Warp jittered rectangular grid to disk.
                const Vec4 diskPoints = {
                    sqrtf(gridPoints[1]) * cosf(M_PIf * 2.0f * gridPoints[0]),
                    sqrtf(gridPoints[1]) * sinf(M_PIf * 2.0f * gridPoints[0]),
                    sqrtf(gridPoints[3]) * cosf(M_PIf * 2.0f * gridPoints[2]),
                    sqrtf(gridPoints[3]) * sinf(M_PIf * 2.0f * gridPoints[2]),
                };

                result.mData[(k * size * size + j * size + i) * 4 + 0]
                    = static_cast<i8>(diskPoints[0] * 127.0f);
                result.mData[(k * size * size + j * size + i) * 4 + 1]
                    = static_cast<i8>(diskPoints[1] * 127.0f);
                result.mData[(k * size * size + j * size + i) * 4 + 2]
                    = static_cast<i8>(diskPoints[2] * 127.0f);
                result.mData[(k * size * size + j * size + i) * 4 + 3]
                    = static_cast<i8>(diskPoints[3] * 127.0f);
            }
        }
    }

    return result;
}

bool Renderer::Init(SDL_Window* window)
{
    assert(window);

    mWindow = window;

    VK_CHECK(volkInitialize());

    // Instance.
    {
        Arena scratch = gArenaReset;

        u32 vulkanApiVersion = 0;
        VK_CHECK(vkEnumerateInstanceVersion(&vulkanApiVersion));
        if (vulkanApiVersion < VK_API_VERSION_1_3)
        {
            fprintf(stderr, "Vulkan API version 1.3 is required\n");
            return false;
        }

        VkApplicationInfo applicationInfo{};
        applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        applicationInfo.pApplicationName = "None";
        applicationInfo.applicationVersion = 1;
        applicationInfo.pEngineName = "None";
        applicationInfo.engineVersion = 1;
        applicationInfo.apiVersion = VK_API_VERSION_1_3;

        u32 sdlExtensionsCount = 0;

        // Find the required KHR surface extensions.
        // Also const in C/C++ is a humiliation ritual, no wonder nobody uses it.
        const char* const* const sdlExtensions
            = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionsCount);
        if (!sdlExtensions)
        {
            fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
            return false;
        }

        u32 availableInstanceExtensionsCount = 0;
        VK_CHECK(vkEnumerateInstanceExtensionProperties(
            nullptr,
            &availableInstanceExtensionsCount,
            nullptr
        ));
        Slice<VkExtensionProperties> instanceExtensionsAvailable{};
        instanceExtensionsAvailable.mCount = static_cast<int>(availableInstanceExtensionsCount);
        instanceExtensionsAvailable.mData
            = scratch.AllocOrDie<VkExtensionProperties>(availableInstanceExtensionsCount);
        VK_CHECK(vkEnumerateInstanceExtensionProperties(
            nullptr,
            &availableInstanceExtensionsCount,
            instanceExtensionsAvailable.mData
        ));

        for (u32 i = 0; i < sdlExtensionsCount; ++i)
        {
            const bool result
                = Vulkan::ExtensionIsAvailable(sdlExtensions[i], instanceExtensionsAvailable);
            if (!result)
            {
                fprintf(stderr, "Required Vulkan extension %s is unavailable\n", sdlExtensions[i]);
                return false;
            }
        }

        VkInstanceCreateInfo instanceCreateInfo{};
        instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceCreateInfo.pApplicationInfo = &applicationInfo;
        instanceCreateInfo.enabledExtensionCount = static_cast<u32>(sdlExtensionsCount);
        instanceCreateInfo.ppEnabledExtensionNames = sdlExtensions;

        VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &mInstance));

        volkLoadInstance(mInstance);
    }

    // NOTE: not enabling validation layers using the Vulkan API, since I find
    // the Vulkan Configurator more convenient.

    // Surface.
    if (!SDL_Vulkan_CreateSurface(window, mInstance, nullptr, &mSurface))
    {
        fprintf(stderr, "SDL_Vulkan_CreateSurface failed\n");
        return false;
    }

    // Physical device.
    const char* const requiredDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    {
        Arena scratch = gArenaReset;

        u32 physicalDeviceCount = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(mInstance, &physicalDeviceCount, nullptr));
        Slice<VkPhysicalDevice> physicalDevices{};
        physicalDevices.mCount = static_cast<int>(physicalDeviceCount);
        physicalDevices.mData = scratch.AllocOrDie<VkPhysicalDevice>(physicalDeviceCount);
        VK_CHECK(
            vkEnumeratePhysicalDevices(mInstance, &physicalDeviceCount, physicalDevices.mData)
        );

        int physicalDeviceIndex = -1;
        for (u32 i = 0; i < physicalDeviceCount; ++i)
        {
            const VkPhysicalDevice physicalDevice = physicalDevices.mData[i];

            // API version.
            VkPhysicalDeviceProperties2 physicalDeviceProperties{};
            physicalDeviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            vkGetPhysicalDeviceProperties2(physicalDevice, &physicalDeviceProperties);
            const bool supportsVulkan13
                = physicalDeviceProperties.properties.apiVersion >= VK_API_VERSION_1_3;

            // Graphics support.
            u32 queueFamilyPropertiesCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties2(
                physicalDevice,
                &queueFamilyPropertiesCount,
                nullptr
            );
            Slice<VkQueueFamilyProperties2> queueFamilyProperties{};
            queueFamilyProperties.mCount = static_cast<int>(queueFamilyPropertiesCount);
            queueFamilyProperties.mData
                = scratch.AllocOrDie<VkQueueFamilyProperties2>(queueFamilyPropertiesCount);
            for (u32 j = 0; j < queueFamilyPropertiesCount; ++j)
            {
                queueFamilyProperties.mData[j].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
            }
            vkGetPhysicalDeviceQueueFamilyProperties2(
                physicalDevice,
                &queueFamilyPropertiesCount,
                queueFamilyProperties.mData
            );
            bool supportsGraphicsAndPresentation = false;
            for (u32 j = 0; j < queueFamilyPropertiesCount; ++j)
            {
                if (queueFamilyProperties.mData[j].queueFamilyProperties.queueFlags
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
            u32 deviceExtensionPropertiesCount = 0;
            VK_CHECK(vkEnumerateDeviceExtensionProperties(
                physicalDevice,
                nullptr,
                &deviceExtensionPropertiesCount,
                nullptr
            ));
            Slice<VkExtensionProperties> extensionProperties{};
            extensionProperties.mCount = static_cast<int>(deviceExtensionPropertiesCount);
            extensionProperties.mData
                = scratch.AllocOrDie<VkExtensionProperties>(deviceExtensionPropertiesCount);
            VK_CHECK(vkEnumerateDeviceExtensionProperties(
                physicalDevice,
                nullptr,
                &deviceExtensionPropertiesCount,
                extensionProperties.mData
            ));
            bool supportsRequiredExtensions = true;
            for (size_t j = 0; j < ARRAY_SIZE(requiredDeviceExtensions); ++j)
            {
                const bool result = Vulkan::ExtensionIsAvailable(
                    requiredDeviceExtensions[j],
                    extensionProperties
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
            const bool supportsRequiredFeatures = vulkanFeatures13.dynamicRendering
                && vulkanFeatures13.synchronization2 && vulkanFeatures12.scalarBlockLayout
                && vulkanFeatures11.shaderDrawParameters
                && physicalDeviceFeatures.features.multiDrawIndirect
                && physicalDeviceFeatures.features.fillModeNonSolid
                && physicalDeviceFeatures.features.depthClamp;

            if (supportsVulkan13 && supportsGraphicsAndPresentation && supportsRequiredExtensions
                && supportsRequiredFeatures)
            {
                physicalDeviceIndex = static_cast<int>(i);
                break;
            }
        }

        if (physicalDeviceIndex < 0)
        {
            fprintf(stderr, "No suitable physical device found\n");
            return false;
        }

        mPhysicalDevice = physicalDevices.mData[physicalDeviceIndex];

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
        VkSampleCountFlags counts = properties.limits.framebufferColorSampleCounts
            & properties.limits.framebufferDepthSampleCounts;
        if (counts & VK_SAMPLE_COUNT_4_BIT)
        {
            mMsaaSamples = VK_SAMPLE_COUNT_4_BIT;
        }
        else if (counts & VK_SAMPLE_COUNT_2_BIT)
        {
            mMsaaSamples = VK_SAMPLE_COUNT_2_BIT;
        }
        else if (counts & VK_SAMPLE_COUNT_1_BIT)
        {
            mMsaaSamples = VK_SAMPLE_COUNT_1_BIT;
        }
        assert(counts & VK_SAMPLE_COUNT_1_BIT);

        Utils::strlcpy(mGpuName, properties.deviceName, sizeof(mGpuName));
    }

    // Logical device, queue.
    {
        // Already checked when picking a physical device.
        Vulkan::QueueInfo queueInfo
            = Vulkan::GetQueue(mPhysicalDevice, VK_QUEUE_GRAPHICS_BIT, gArenaReset);

        VkPhysicalDeviceVulkan13Features vulkanFeatures13{};
        vulkanFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkanFeatures13.dynamicRendering = VK_TRUE;
        vulkanFeatures13.synchronization2 = VK_TRUE;
        VkPhysicalDeviceVulkan12Features vulkanFeatures12{};
        vulkanFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vulkanFeatures12.pNext = &vulkanFeatures13;
        vulkanFeatures12.scalarBlockLayout = VK_TRUE;
        VkPhysicalDeviceVulkan11Features vulkanFeatures11{};
        vulkanFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        vulkanFeatures11.pNext = &vulkanFeatures12;
        vulkanFeatures11.shaderDrawParameters = true;
        VkPhysicalDeviceFeatures2 physicalDeviceFeatures{};
        physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        physicalDeviceFeatures.pNext = &vulkanFeatures11;
        physicalDeviceFeatures.features.multiDrawIndirect = VK_TRUE;
        physicalDeviceFeatures.features.fillModeNonSolid = VK_TRUE;
        physicalDeviceFeatures.features.depthClamp = VK_TRUE;

        constexpr f32 queuePriority = 1.0f;

        VkDeviceQueueCreateInfo deviceQueueCreateInfo{};
        deviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        deviceQueueCreateInfo.queueFamilyIndex = queueInfo.mQueueIndex;
        deviceQueueCreateInfo.queueCount = 1;
        deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pNext = &physicalDeviceFeatures;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
        deviceCreateInfo.enabledExtensionCount
            = static_cast<u32>(ARRAY_SIZE(requiredDeviceExtensions));
        deviceCreateInfo.ppEnabledExtensionNames = requiredDeviceExtensions;

        VK_CHECK(vkCreateDevice(mPhysicalDevice, &deviceCreateInfo, nullptr, &mDevice));
        vkGetDeviceQueue(mDevice, queueInfo.mFamilyIndex, queueInfo.mQueueIndex, &queueInfo.mQueue);
        mQueueInfo = queueInfo;

        volkLoadDevice(mDevice);
    }

    if (!RecreateSwapchain())
    {
        return false;
    }

    // Shadow map resources.
    {
        constexpr VkFormat SHADOW_DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(
            mPhysicalDevice,
            VK_FORMAT_D32_SFLOAT,
            &formatProperties
        );
        if (!(formatProperties.optimalTilingFeatures
              & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
            fprintf(stderr, "Vulkan failed to find a suitable shadow map depth format\n");
            return false;
        }
        mShadowDepthFormat = SHADOW_DEPTH_FORMAT;

        VkExtent3D extent{};
        extent.width = RENDERER_SHADOW_MAP_DIMENSIONS;
        extent.height = RENDERER_SHADOW_MAP_DIMENSIONS;
        extent.depth = 1;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = mShadowDepthFormat;
        imageInfo.extent = extent;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = RENDERER_SHADOW_MAP_CASCADE_COUNT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK(vkCreateImage(mDevice, &imageInfo, nullptr, &mShadowMapImage.mImage));

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(mDevice, mShadowMapImage.mImage, &memoryRequirements);

        u32 memoryTypeIndex = 0;
        bool result = Vulkan::FindMemoryType(
            memoryTypeIndex,
            mPhysicalDevice,
            memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        if (!result)
        {
            fprintf(stderr, "Vulkan failed to find a suitable memory type\n");
            return false;
        }

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = memoryTypeIndex;
        VK_CHECK(vkAllocateMemory(mDevice, &allocateInfo, nullptr, &mShadowMapImage.mMemory));

        VK_CHECK(vkBindImageMemory(mDevice, mShadowMapImage.mImage, mShadowMapImage.mMemory, 0));

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.image = mShadowMapImage.mImage;
        viewInfo.format = mShadowDepthFormat;
        viewInfo.subresourceRange = {};
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.layerCount = RENDERER_SHADOW_MAP_CASCADE_COUNT;
        viewInfo.subresourceRange.levelCount = 1;
        VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mShadowMapImage.mView));

        // One image view per cascade.
        for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
        {
            VkImageViewCreateInfo imageViewInfo{};
            imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            imageViewInfo.image = mShadowMapImage.mImage;
            imageViewInfo.format = mShadowDepthFormat;
            imageViewInfo.subresourceRange = {};
            imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            imageViewInfo.subresourceRange.layerCount = 1;
            imageViewInfo.subresourceRange.baseArrayLayer = static_cast<u32>(i);
            imageViewInfo.subresourceRange.levelCount = 1;
            VK_CHECK(
                vkCreateImageView(mDevice, &imageViewInfo, nullptr, &mShadowMapImageViewCascade[i])
            );
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = VK_COMPARE_OP_GREATER;
        VK_CHECK(vkCreateSampler(mDevice, &samplerInfo, nullptr, &mShadowMapImage.mSampler));

        constexpr f32 clipRange = SHADOW_FAR_PLANE - NEAR_PLANE;

        constexpr f32 minZ = NEAR_PLANE;
        constexpr f32 maxZ = NEAR_PLANE + clipRange;

        constexpr f32 range = maxZ - minZ;
        constexpr f32 ratio = maxZ / minZ;

        for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
        {
            const f32 p
                = static_cast<f32>(i + 1) / static_cast<f32>(RENDERER_SHADOW_MAP_CASCADE_COUNT);
            const f32 log = minZ * powf(ratio, p);
            const f32 uniform = minZ + range * p;
            const f32 d = SHADOW_MAP_CASCADE_SPLIT_LAMBDA * (log - uniform) + uniform;
            mShadowCascadeSplitDepths[i] = -d;
        }
    }

    // Shadow map jitter image for PCF.
    {
        static constexpr VkFormat SHADOW_MAP_JITTER_OFFSETS_IMAGE_FORMAT = VK_FORMAT_R8G8B8A8_SNORM;
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(
            mPhysicalDevice,
            SHADOW_MAP_JITTER_OFFSETS_IMAGE_FORMAT,
            &formatProperties
        );
        constexpr u64 requiredBits
            = VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if (!(formatProperties.optimalTilingFeatures & requiredBits))
        {
            fprintf(stderr, "Vulkan failed to find a suitable jitter offsets image format\n");
            return false;
        }

        VkExtent3D extent{};
        extent.width = RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE;
        extent.height = RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE;
        extent.depth = RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U
            * RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V / 2;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
        imageInfo.format = SHADOW_MAP_JITTER_OFFSETS_IMAGE_FORMAT;
        imageInfo.extent = extent;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK(vkCreateImage(mDevice, &imageInfo, nullptr, &mShadowJitterOffsetsImage.mImage));

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(
            mDevice,
            mShadowJitterOffsetsImage.mImage,
            &memoryRequirements
        );

        u32 memoryTypeIndex = 0;
        bool result = Vulkan::FindMemoryType(
            memoryTypeIndex,
            mPhysicalDevice,
            memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        if (!result)
        {
            fprintf(stderr, "Vulkan failed to find a suitable memory type\n");
            return false;
        }

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = memoryTypeIndex;
        VK_CHECK(
            vkAllocateMemory(mDevice, &allocateInfo, nullptr, &mShadowJitterOffsetsImage.mMemory)
        );

        VK_CHECK(vkBindImageMemory(
            mDevice,
            mShadowJitterOffsetsImage.mImage,
            mShadowJitterOffsetsImage.mMemory,
            0
        ));

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.layerCount = 1;
        subresourceRange.levelCount = 1;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = mShadowJitterOffsetsImage.mImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewInfo.format = SHADOW_MAP_JITTER_OFFSETS_IMAGE_FORMAT;
        viewInfo.subresourceRange = subresourceRange;
        VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mShadowJitterOffsetsImage.mView));
    }

    // Sampler to fetch the shadow jittered offsets.
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VK_CHECK(
            vkCreateSampler(mDevice, &samplerInfo, nullptr, &mShadowJitterOffsetsImage.mSampler)
        );
    }

    // Main descriptor set layout.
    {
        // TODO: use the slang reflection API, this is some caveman shit.
        VkDescriptorSetLayoutBinding uniformLayoutBinding{};
        uniformLayoutBinding.binding = 0;
        uniformLayoutBinding.descriptorCount = 1;
        uniformLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding storageLayoutBinding{};
        storageLayoutBinding.binding = 1;
        storageLayoutBinding.descriptorCount = 1;
        storageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        storageLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutBinding drawDataLayoutBinding{};
        drawDataLayoutBinding.binding = 2;
        drawDataLayoutBinding.descriptorCount = 1;
        drawDataLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        drawDataLayoutBinding.stageFlags
            = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding shadowMapLayoutBinding{};
        shadowMapLayoutBinding.binding = 4;
        shadowMapLayoutBinding.descriptorCount = 1;
        shadowMapLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowMapLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding shadowJitterLayoutBinding{};
        shadowJitterLayoutBinding.binding = 5;
        shadowJitterLayoutBinding.descriptorCount = 1;
        shadowJitterLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowJitterLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // clang-format off
        const VkDescriptorSetLayoutBinding layoutBindings[] = {
            uniformLayoutBinding,
            storageLayoutBinding,
            drawDataLayoutBinding,
            shadowMapLayoutBinding,
            shadowJitterLayoutBinding
        };
        // clang-format on

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = ARRAY_SIZE(layoutBindings);
        layoutInfo.pBindings = layoutBindings;
        VK_CHECK(vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mDescriptorSetLayout));
    }

    // Line descriptor set layout.
    {
        VkDescriptorSetLayoutBinding uniformLayoutBinding{};
        uniformLayoutBinding.binding = 0;
        uniformLayoutBinding.descriptorCount = 1;
        uniformLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding lineDataLayoutBinding{};
        lineDataLayoutBinding.binding = 1;
        lineDataLayoutBinding.descriptorCount = 1;
        lineDataLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lineDataLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        // clang-format off
        const VkDescriptorSetLayoutBinding layoutBindings[] = {
            uniformLayoutBinding,
            lineDataLayoutBinding,
        };
        // clang-format on

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = ARRAY_SIZE(layoutBindings);
        layoutInfo.pBindings = layoutBindings;
        VK_CHECK(
            vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mLineDescriptorSetLayout)
        );
    }

    // Graphics pipelines.
    {
        VkShaderModule shaderModule{};
        if (!Vulkan::CreateShaderModule(shaderModule, mDevice, "Renderer.slang.spv"))
        {
            return false;
        }
        DEFER(vkDestroyShaderModule(mDevice, shaderModule, nullptr));

        VkShaderModule lineShaderModule{};
        if (!Vulkan::CreateShaderModule(lineShaderModule, mDevice, "LineRenderer.slang.spv"))
        {
            return false;
        }
        DEFER(vkDestroyShaderModule(mDevice, lineShaderModule, nullptr));

        VkShaderModule shadowShaderModule{};
        if (!Vulkan::CreateShaderModule(shadowShaderModule, mDevice, "ShadowPass.slang.spv"))
        {
            return false;
        }
        DEFER(vkDestroyShaderModule(mDevice, shadowShaderModule, nullptr));

        VkPipelineShaderStageCreateInfo vertexStageInfo{};
        vertexStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertexStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertexStageInfo.module = shaderModule;
        vertexStageInfo.pName = "vertexMain";

        VkPipelineShaderStageCreateInfo fragmentStageInfo{};
        fragmentStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragmentStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragmentStageInfo.module = shaderModule;
        fragmentStageInfo.pName = "fragmentMain";

        const VkPipelineShaderStageCreateInfo shaderStages[] = {vertexStageInfo, fragmentStageInfo};

        vertexStageInfo.module = lineShaderModule;
        fragmentStageInfo.module = lineShaderModule;

        const VkPipelineShaderStageCreateInfo lineShaderStages[]
            = {vertexStageInfo, fragmentStageInfo};

        vertexStageInfo.module = shadowShaderModule;
        fragmentStageInfo.module = shadowShaderModule;

        const VkPipelineShaderStageCreateInfo shadowShaderStages[]
            = {vertexStageInfo, fragmentStageInfo};

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineViewportStateCreateInfo viewportStateInfo{};
        viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateInfo.viewportCount = 1;
        viewportStateInfo.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
        rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationInfo.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampleInfo{};
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.rasterizationSamples = mMsaaSamples;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;

        // clang-format off
        constexpr VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        // clang-format on

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<u32>(ARRAY_SIZE(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &mDescriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        VK_CHECK(vkCreatePipelineLayout(mDevice, &pipelineLayoutInfo, nullptr, &mPipelineLayout));

        VK_CHECK(
            vkCreatePipelineLayout(mDevice, &pipelineLayoutInfo, nullptr, &mShadowPipelineLayout)
        );

        pipelineLayoutInfo.pSetLayouts = &mLineDescriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        VK_CHECK(
            vkCreatePipelineLayout(mDevice, &pipelineLayoutInfo, nullptr, &mLinePipelineLayout)
        );

        VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
        pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingInfo.colorAttachmentCount = 1;
        pipelineRenderingInfo.pColorAttachmentFormats = &mSwapchainSurfaceFormat.format;
        pipelineRenderingInfo.depthAttachmentFormat = mDepthFormat;

        VkGraphicsPipelineCreateInfo graphicsPipelineInfo{};
        graphicsPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        graphicsPipelineInfo.pNext = &pipelineRenderingInfo;
        graphicsPipelineInfo.stageCount = 2;
        graphicsPipelineInfo.pStages = shaderStages;
        graphicsPipelineInfo.pVertexInputState = &vertexInputInfo;
        graphicsPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
        graphicsPipelineInfo.pViewportState = &viewportStateInfo;
        graphicsPipelineInfo.pRasterizationState = &rasterizationInfo;
        graphicsPipelineInfo.pMultisampleState = &multisampleInfo;
        graphicsPipelineInfo.pColorBlendState = &colorBlending;
        graphicsPipelineInfo.pDepthStencilState = &depthStencil;
        graphicsPipelineInfo.pDynamicState = &dynamicState;
        graphicsPipelineInfo.layout = mPipelineLayout;

        VK_CHECK(vkCreateGraphicsPipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &graphicsPipelineInfo,
            nullptr,
            &mGraphicsPipeline
        ));

        rasterizationInfo.depthClampEnable = VK_TRUE;
        colorBlending.attachmentCount = 0;
        pipelineRenderingInfo.colorAttachmentCount = 0;
        pipelineRenderingInfo.depthAttachmentFormat = mShadowDepthFormat;
        // rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
        graphicsPipelineInfo.pStages = shadowShaderStages;
        graphicsPipelineInfo.layout = mShadowPipelineLayout;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VK_CHECK(vkCreateGraphicsPipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &graphicsPipelineInfo,
            nullptr,
            &mGraphicsPipelineShadow
        ));

        pipelineLayoutInfo.pushConstantRangeCount = 0;
        colorBlending.attachmentCount = 1;
        pipelineRenderingInfo.depthAttachmentFormat = mDepthFormat;
        pipelineRenderingInfo.colorAttachmentCount = 1;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
        rasterizationInfo.depthClampEnable = VK_FALSE;
        graphicsPipelineInfo.pStages = lineShaderStages;
        graphicsPipelineInfo.layout = mLinePipelineLayout;
        multisampleInfo.rasterizationSamples = mMsaaSamples;

        VK_CHECK(vkCreateGraphicsPipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &graphicsPipelineInfo,
            nullptr,
            &mGraphicsPipelineLines
        ));
    }

    // Command pool.
    {
        VkCommandPoolCreateInfo cmdPoolInfo{};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmdPoolInfo.queueFamilyIndex = mQueueInfo.mFamilyIndex;
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
                vkAllocateCommandBuffers(mDevice, &cmdBufferAllocateInfo, &mFrame[i].mCommandBuffer)
            );
        }
    }

    // Synchronization primitives.
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        mRenderFinishedSemaphores.mCount = mSwapchainImages.mCount;
        mRenderFinishedSemaphores.mData
            = gArenaStatic.AllocOrDie<VkSemaphore>(mRenderFinishedSemaphores.mCount);
        for (int i = 0; i < mRenderFinishedSemaphores.mCount; ++i)
        {
            VkSemaphore semaphore{};
            VK_CHECK(vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &semaphore));
            mRenderFinishedSemaphores.mData[i] = semaphore;
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VK_CHECK(vkCreateFence(mDevice, &fenceInfo, nullptr, &mFrame[i].mQueueSubmitFence));
            VK_CHECK(vkCreateSemaphore(
                mDevice,
                &semaphoreInfo,
                nullptr,
                &mFrame[i].mImageAcquireSemaphore
            ));
        }
    }

    // Single buffer for vertex and index data.
    {
        Slice<Vertex> vertices{};
        Slice<u16> indices{};

        Arena scratch = gArenaReset;

        Slice<Vec3> cubePositions{};
        Slice<Vec3> cubeNormals{};
        Slice<u16> cubeIndices{};
        GetCubeData(cubePositions, cubeIndices, &cubeNormals, scratch);

        Slice<Vec3> spherePositions{};
        Slice<Vec3> sphereNormals{};
        Slice<u16> sphereIndices{};
        GetSphereData(spherePositions, sphereIndices, &sphereNormals, scratch);

        Slice<Vec3> tetrahedronPositions{};
        Slice<Vec3> tetrahedronNormals{};
        Slice<u16> tetrahedronIndices{};
        GetTetrahedronData(tetrahedronPositions, tetrahedronIndices, &tetrahedronNormals, scratch);

        vertices.mCount
            = cubePositions.mCount + spherePositions.mCount + tetrahedronPositions.mCount;
        vertices.mData = scratch.AllocOrDie<Vertex>(vertices.mCount);

        indices.mCount = cubeIndices.mCount + sphereIndices.mCount + tetrahedronIndices.mCount;
        indices.mData = scratch.AllocOrDie<u16>(indices.mCount);

        int index = 0;
        int vertex = 0;

        mDrawCommandCube = {};
        mDrawCommandCube.indexCount = static_cast<u32>(cubeIndices.mCount);
        mDrawCommandCube.instanceCount = 1;
        mDrawCommandCube.firstIndex = static_cast<u32>(index);
        mDrawCommandCube.vertexOffset = vertex;

        index += cubeIndices.mCount;
        vertex += cubePositions.mCount;

        for (int i = 0; i < cubePositions.mCount; ++i)
        {
            const int dataIndex = mDrawCommandCube.vertexOffset + i;
            vertices.mData[dataIndex].mPosition = cubePositions.mData[i];
            vertices.mData[dataIndex].mNormal = cubeNormals.mData[i];
        }
        for (u32 i = 0; i < static_cast<u32>(cubeIndices.mCount); ++i)
        {
            const u32 dataIndex = mDrawCommandCube.firstIndex + i;
            indices.mData[dataIndex] = cubeIndices.mData[i];
        }

        mDrawCommandSphere = {};
        mDrawCommandSphere.indexCount = static_cast<u32>(sphereIndices.mCount);
        mDrawCommandSphere.instanceCount = 1;
        mDrawCommandSphere.firstIndex = static_cast<u32>(index);
        mDrawCommandSphere.vertexOffset = vertex;

        index += sphereIndices.mCount;
        vertex += spherePositions.mCount;

        for (int i = 0; i < spherePositions.mCount; ++i)
        {
            const int dataIndex = mDrawCommandSphere.vertexOffset + i;
            vertices.mData[dataIndex].mPosition = spherePositions.mData[i];
            vertices.mData[dataIndex].mNormal = sphereNormals.mData[i];
        }
        for (u32 i = 0; i < static_cast<u32>(sphereIndices.mCount); ++i)
        {
            const u32 dataIndex = mDrawCommandSphere.firstIndex + i;
            indices.mData[dataIndex] = sphereIndices.mData[i];
        }

        mDrawCommandTetrahedron = {};
        mDrawCommandTetrahedron.indexCount = static_cast<u32>(tetrahedronIndices.mCount);
        mDrawCommandTetrahedron.instanceCount = 1;
        mDrawCommandTetrahedron.firstIndex = static_cast<u32>(index);
        mDrawCommandTetrahedron.vertexOffset = vertex;

        index += tetrahedronIndices.mCount;
        vertex += tetrahedronPositions.mCount;

        for (int i = 0; i < tetrahedronPositions.mCount; ++i)
        {
            const int dataIndex = mDrawCommandTetrahedron.vertexOffset + i;
            vertices.mData[dataIndex].mPosition = tetrahedronPositions.mData[i];
            vertices.mData[dataIndex].mNormal = tetrahedronNormals.mData[i];
        }
        for (u32 i = 0; i < static_cast<u32>(tetrahedronIndices.mCount); ++i)
        {
            const u32 dataIndex = mDrawCommandTetrahedron.firstIndex + i;
            indices.mData[dataIndex] = tetrahedronIndices.mData[i];
        }

        assert(vertex == vertices.mCount);
        assert(index == indices.mCount);
        mVerticesCount = vertices.mCount;
        mIndicesCount = indices.mCount;

        const size_t verticesSize = static_cast<size_t>(vertices.GetSizeBytes());
        const size_t indicesSize = static_cast<size_t>(indices.GetSizeBytes());
        const size_t bufferSize = verticesSize + indicesSize;

        VkBuffer stagingBuffer{};
        VkDeviceMemory stagingBufferMemory{};
        bool result = Vulkan::CreateBuffer(
            stagingBuffer,
            stagingBufferMemory,
            mPhysicalDevice,
            mDevice,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        if (!result)
        {
            fprintf(stderr, "Vulkan failed to create a staging buffer\n");
            return false;
        }
        DEFER(vkFreeMemory(mDevice, stagingBufferMemory, nullptr));
        DEFER(vkDestroyBuffer(mDevice, stagingBuffer, nullptr));
        void* data{};
        VK_CHECK(vkMapMemory(mDevice, stagingBufferMemory, 0, VK_WHOLE_SIZE, 0, &data));
        memcpy(data, vertices.mData, verticesSize);
        memcpy(static_cast<u8*>(data) + verticesSize, indices.mData, indicesSize);
        vkUnmapMemory(mDevice, stagingBufferMemory);

        result = Vulkan::CreateBuffer(
            mVertexIndexBuffer.mBuffer,
            mVertexIndexBuffer.mMemory,
            mPhysicalDevice,
            mDevice,
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        if (!result)
        {
            fprintf(stderr, "Vulkan failed to create a buffer for vertex and index data\n");
            return false;
        }

        result = Vulkan::CopyBuffer(
            mVertexIndexBuffer.mBuffer,
            stagingBuffer,
            mDevice,
            mCommandPool,
            mQueueInfo.mQueue,
            bufferSize
        );
        if (!result)
        {
            fprintf(
                stderr,
                "Vulkan failed to copy the staging buffer to the vertex/index buffer\n"
            );
            return false;
        }
    }

    // Uniform buffer.
    {
        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            // NOTE: creating a host visible, coherent, device local buffer.
            // Should be legal even on discrete GPUs if total allocated
            // size is less than 200 MB or so.
            const bool result = Vulkan::CreateBuffer(
                mFrame[i].mUniformDataBuffer.mBuffer,
                mFrame[i].mUniformDataBuffer.mMemory,
                mPhysicalDevice,
                mDevice,
                sizeof(UniformData) * MAX_DRAW_CALLS,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                    | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );
            if (!result)
            {
                fprintf(stderr, "Vulkan failed to create a uniform buffer\n");
                return false;
            }

            VK_CHECK(vkMapMemory(
                mDevice,
                mFrame[i].mUniformDataBuffer.mMemory,
                0,
                sizeof(UniformData),
                0,
                &mFrame[i].mUniformDataBuffer.mMapped
            ));
        }
    }

    // Draw data buffer.
    {
        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            // NOTE: creating a host visible, coherent, device local buffer.
            // Should be legal even on discrete GPUs if total allocated
            // size is less than 200 MB or so.
            const bool result = Vulkan::CreateBuffer(
                mFrame[i].mDrawDataBuffer.mBuffer,
                mFrame[i].mDrawDataBuffer.mMemory,
                mPhysicalDevice,
                mDevice,
                sizeof(DrawData) * MAX_DRAW_CALLS,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                    | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );
            if (!result)
            {
                fprintf(stderr, "Vulkan failed to create a draw buffer\n");
                return false;
            }

            VK_CHECK(vkMapMemory(
                mDevice,
                mFrame[i].mDrawDataBuffer.mMemory,
                0,
                sizeof(DrawData) * MAX_DRAW_CALLS,
                0,
                &mFrame[i].mDrawDataBuffer.mMapped
            ));
        }
    }

    // Separate line buffer.
    {
        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            const bool result = Vulkan::CreateBuffer(
                mFrame[i].mLineDataBuffer.mBuffer,
                mFrame[i].mLineDataBuffer.mMemory,
                mPhysicalDevice,
                mDevice,
                sizeof(LineData) * MAX_DRAW_CALLS,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                    | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );
            if (!result)
            {
                fprintf(stderr, "Vulkan failed to create a line buffer\n");
                return false;
            }

            VK_CHECK(vkMapMemory(
                mDevice,
                mFrame[i].mLineDataBuffer.mMemory,
                0,
                sizeof(LineData) * MAX_DRAW_CALLS,
                0,
                &mFrame[i].mLineDataBuffer.mMapped
            ));
        }
    }

    // Draw indirect commands buffer.
    {
        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            const bool result = Vulkan::CreateBuffer(
                mFrame[i].mDrawIndirectBuffer.mBuffer,
                mFrame[i].mDrawIndirectBuffer.mMemory,
                mPhysicalDevice,
                mDevice,
                sizeof(DrawData) * MAX_DRAW_CALLS,
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                    | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );
            if (!result)
            {
                fprintf(stderr, "Vulkan failed to create a draw indirect buffer\n");
                return false;
            }

            VK_CHECK(vkMapMemory(
                mDevice,
                mFrame[i].mDrawIndirectBuffer.mMemory,
                0,
                sizeof(DrawData) * MAX_DRAW_CALLS,
                0,
                &mFrame[i].mDrawIndirectBuffer.mMapped
            ));
        }
    }

    // Upload shadow jitter offset data to the texture.
    {
        Arena scratch = gArenaReset;
        Slice<i8> jitterOffsets = CreateShadowJitterOffsets(
            RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
            RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U,
            RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V,
            scratch
        );

        VkBuffer stagingBuffer{};
        VkDeviceMemory stagingBufferMemory{};
        const VkDeviceSize uploadSize = static_cast<VkDeviceSize>(jitterOffsets.GetSizeBytes());
        const bool result = Vulkan::CreateBuffer(
            stagingBuffer,
            stagingBufferMemory,
            mPhysicalDevice,
            mDevice,
            uploadSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        if (!result)
        {
            fprintf(stderr, "Vulkan failed to create a staging buffer\n");
            return false;
        }
        DEFER(vkDestroyBuffer(mDevice, stagingBuffer, nullptr));
        DEFER(vkFreeMemory(mDevice, stagingBufferMemory, nullptr));

        void* mapped{};
        VK_CHECK(vkMapMemory(mDevice, stagingBufferMemory, 0, VK_WHOLE_SIZE, 0, &mapped));
        memcpy(mapped, jitterOffsets.mData, uploadSize);

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool = mCommandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VkCommandBuffer copyCmdBuffer{};
        VK_CHECK(vkAllocateCommandBuffers(mDevice, &allocateInfo, &copyCmdBuffer));
        DEFER(vkFreeCommandBuffers(mDevice, mCommandPool, 1, &copyCmdBuffer));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(copyCmdBuffer, &beginInfo));

        Vulkan::ImageMemoryBarrier(
            copyCmdBuffer,
            mShadowJitterOffsetsImage.mImage,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        VkImageSubresourceLayers imageSubresource{};
        imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageSubresource.layerCount = 1;

        VkBufferImageCopy bufferCopyRegion{};
        bufferCopyRegion.imageSubresource = imageSubresource;
        // clang-format off
        bufferCopyRegion.imageExtent = {
            static_cast<u32>(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE),
            static_cast<u32>(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE),
            static_cast<u32>(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U *
                             RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V / 2)
        };
        // clang-format on
        vkCmdCopyBufferToImage(
            copyCmdBuffer,
            stagingBuffer,
            mShadowJitterOffsetsImage.mImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &bufferCopyRegion
        );

        Vulkan::ImageMemoryBarrier(
            copyCmdBuffer,
            mShadowJitterOffsetsImage.mImage,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT
        );

        VK_CHECK(vkEndCommandBuffer(copyCmdBuffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copyCmdBuffer;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence{};
        VK_CHECK(vkCreateFence(mDevice, &fenceInfo, nullptr, &fence));
        DEFER(vkDestroyFence(mDevice, fence, nullptr));

        VK_CHECK(vkQueueSubmit(mQueueInfo.mQueue, 1, &submitInfo, fence));

        VK_CHECK(vkWaitForFences(mDevice, 1, &fence, VK_TRUE, 1'000'000'000));
    }

    // Descriptor pool.
    {
        // clang-format off
        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RENDERER_MAX_FRAMES_IN_FLIGHT * 2},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RENDERER_MAX_FRAMES_IN_FLIGHT},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RENDERER_MAX_FRAMES_IN_FLIGHT * 2},
        };
        // clang-format on

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.poolSizeCount = ARRAY_SIZE(poolSizes);
        poolInfo.pPoolSizes = poolSizes;
        for (const VkDescriptorPoolSize& poolSize : poolSizes)
        {
            poolInfo.maxSets += poolSize.descriptorCount;
        }
        VK_CHECK(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mDescriptorPool));
    }

    // Main descriptor sets.
    {
        VkDescriptorSetLayout layouts[RENDERER_MAX_FRAMES_IN_FLIGHT]{};
        for (size_t i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            layouts[i] = mDescriptorSetLayout;
        }

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = mDescriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = layouts;

        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VK_CHECK(vkAllocateDescriptorSets(mDevice, &allocateInfo, &mFrame[i].mDescriptorSet));
        }

        const size_t vertexIndexBufferSize = static_cast<size_t>(mVerticesCount) * sizeof(Vertex)
            + static_cast<size_t>(mIndicesCount) * sizeof(u16);

        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VkDescriptorBufferInfo uniformBufferInfo{};
            uniformBufferInfo.buffer = mFrame[i].mUniformDataBuffer.mBuffer;
            uniformBufferInfo.range = sizeof(UniformData);

            // TODO: use the slang reflection API, this is some caveman shit.
            VkWriteDescriptorSet uniformDescriptorWrite{};
            uniformDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            uniformDescriptorWrite.dstSet = mFrame[i].mDescriptorSet;
            uniformDescriptorWrite.dstBinding = 0;
            uniformDescriptorWrite.descriptorCount = 1;
            uniformDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uniformDescriptorWrite.pBufferInfo = &uniformBufferInfo;

            VkDescriptorBufferInfo storageBufferInfo{};
            storageBufferInfo.buffer = mVertexIndexBuffer.mBuffer;
            storageBufferInfo.range = vertexIndexBufferSize;

            VkWriteDescriptorSet storageDescriptorWrite{};
            storageDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            storageDescriptorWrite.dstSet = mFrame[i].mDescriptorSet;
            storageDescriptorWrite.dstBinding = 1;
            storageDescriptorWrite.descriptorCount = 1;
            storageDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            storageDescriptorWrite.pBufferInfo = &storageBufferInfo;

            VkDescriptorBufferInfo drawDataBufferInfo{};
            drawDataBufferInfo.buffer = mFrame[i].mDrawDataBuffer.mBuffer;
            drawDataBufferInfo.range = sizeof(DrawData) * MAX_DRAW_CALLS;

            VkWriteDescriptorSet drawDataDescriptorWrite{};
            drawDataDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            drawDataDescriptorWrite.dstSet = mFrame[i].mDescriptorSet;
            drawDataDescriptorWrite.dstBinding = 2;
            drawDataDescriptorWrite.descriptorCount = 1;
            drawDataDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            drawDataDescriptorWrite.pBufferInfo = &drawDataBufferInfo;

            VkDescriptorImageInfo shadowMapImageInfo{};
            shadowMapImageInfo.sampler = mShadowMapImage.mSampler;
            shadowMapImageInfo.imageView = mShadowMapImage.mView;
            shadowMapImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet shadowMapDescriptorWrite{};
            shadowMapDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            shadowMapDescriptorWrite.dstSet = mFrame[i].mDescriptorSet;
            shadowMapDescriptorWrite.dstBinding = 4;
            shadowMapDescriptorWrite.descriptorCount = 1;
            shadowMapDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            shadowMapDescriptorWrite.pImageInfo = &shadowMapImageInfo;

            VkDescriptorImageInfo shadowJitterImageInfo{};
            shadowJitterImageInfo.sampler = mShadowJitterOffsetsImage.mSampler;
            shadowJitterImageInfo.imageView = mShadowJitterOffsetsImage.mView;
            shadowJitterImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet shadowJitterDescriptorWrite{};
            shadowJitterDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            shadowJitterDescriptorWrite.dstSet = mFrame[i].mDescriptorSet;
            shadowJitterDescriptorWrite.dstBinding = 5;
            shadowJitterDescriptorWrite.descriptorCount = 1;
            shadowJitterDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            shadowJitterDescriptorWrite.pImageInfo = &shadowJitterImageInfo;

            // clang-format off
            const VkWriteDescriptorSet descriptorsWrite[] = {
                uniformDescriptorWrite,
                storageDescriptorWrite,
                drawDataDescriptorWrite,
                shadowMapDescriptorWrite,
                shadowJitterDescriptorWrite
            };
            // clang-format on

            vkUpdateDescriptorSets(
                mDevice,
                ARRAY_SIZE(descriptorsWrite),
                descriptorsWrite,
                0,
                nullptr
            );
        }
    }

    // Line descriptor sets.
    {
        VkDescriptorSetLayout layouts[RENDERER_MAX_FRAMES_IN_FLIGHT]{};
        for (size_t i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            layouts[i] = mLineDescriptorSetLayout;
        }

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = mDescriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = layouts;

        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VK_CHECK(
                vkAllocateDescriptorSets(mDevice, &allocateInfo, &mFrame[i].mLineDescriptorSet)
            );
        }

        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VkDescriptorBufferInfo uniformBufferInfo{};
            uniformBufferInfo.buffer = mFrame[i].mUniformDataBuffer.mBuffer;
            uniformBufferInfo.range = sizeof(UniformData);

            VkWriteDescriptorSet uniformDescriptorWrite{};
            uniformDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            uniformDescriptorWrite.dstSet = mFrame[i].mLineDescriptorSet;
            uniformDescriptorWrite.dstBinding = 0;
            uniformDescriptorWrite.descriptorCount = 1;
            uniformDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uniformDescriptorWrite.pBufferInfo = &uniformBufferInfo;

            VkDescriptorBufferInfo lineDataBufferInfo{};
            lineDataBufferInfo.buffer = mFrame[i].mLineDataBuffer.mBuffer;
            lineDataBufferInfo.range = sizeof(LineData) * MAX_DRAW_CALLS;

            VkWriteDescriptorSet lineDataDescriptorWrite{};
            lineDataDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            lineDataDescriptorWrite.dstSet = mFrame[i].mLineDescriptorSet;
            lineDataDescriptorWrite.dstBinding = 1;
            lineDataDescriptorWrite.descriptorCount = 1;
            lineDataDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            lineDataDescriptorWrite.pBufferInfo = &lineDataBufferInfo;

            // clang-format off
            const VkWriteDescriptorSet descriptorsWrite[] = {
                uniformDescriptorWrite,
                lineDataDescriptorWrite
            };
            // clang-format on

            vkUpdateDescriptorSets(
                mDevice,
                ARRAY_SIZE(descriptorsWrite),
                descriptorsWrite,
                0,
                nullptr
            );
        }
    }

    if (!mImguiRenderer.Init(
            mWindow,
            mPhysicalDevice,
            mDevice,
            mCommandPool,
            mQueueInfo,
            mSwapchainSurfaceFormat.format,
            mDepthFormat
        ))
    {
        fprintf(stderr, "Failed to initialize ImGui renderer\n");
        return false;
    }

    return true;
}

void Renderer::Cleanup()
{
    vkDeviceWaitIdle(mDevice);

    mImguiRenderer.Cleanup();

    vkDestroySampler(mDevice, mShadowMapImage.mSampler, nullptr);
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        vkDestroyImageView(mDevice, mShadowMapImageViewCascade[i], nullptr);
    }
    vkDestroySampler(mDevice, mShadowJitterOffsetsImage.mSampler, nullptr);
    vkDestroyImageView(mDevice, mShadowJitterOffsetsImage.mView, nullptr);
    vkFreeMemory(mDevice, mShadowJitterOffsetsImage.mMemory, nullptr);
    vkDestroyImage(mDevice, mShadowJitterOffsetsImage.mImage, nullptr);
    vkDestroyImageView(mDevice, mShadowMapImage.mView, nullptr);
    vkFreeMemory(mDevice, mShadowMapImage.mMemory, nullptr);
    vkDestroyImage(mDevice, mShadowMapImage.mImage, nullptr);
    CleanupColorResources();
    CleanupDepthResources();
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkFreeMemory(mDevice, mFrame[i].mUniformDataBuffer.mMemory, nullptr);
        vkDestroyBuffer(mDevice, mFrame[i].mUniformDataBuffer.mBuffer, nullptr);
        vkFreeMemory(mDevice, mFrame[i].mDrawDataBuffer.mMemory, nullptr);
        vkDestroyBuffer(mDevice, mFrame[i].mDrawDataBuffer.mBuffer, nullptr);
        vkFreeMemory(mDevice, mFrame[i].mDrawIndirectBuffer.mMemory, nullptr);
        vkDestroyBuffer(mDevice, mFrame[i].mDrawIndirectBuffer.mBuffer, nullptr);
        vkFreeMemory(mDevice, mFrame[i].mLineDataBuffer.mMemory, nullptr);
        vkDestroyBuffer(mDevice, mFrame[i].mLineDataBuffer.mBuffer, nullptr);
    }
    vkDestroyDescriptorSetLayout(mDevice, mDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(mDevice, mLineDescriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);
    vkFreeMemory(mDevice, mVertexIndexBuffer.mMemory, nullptr);
    vkDestroyBuffer(mDevice, mVertexIndexBuffer.mBuffer, nullptr);
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroyFence(mDevice, mFrame[i].mQueueSubmitFence, nullptr);
        vkDestroySemaphore(mDevice, mFrame[i].mImageAcquireSemaphore, nullptr);
    }
    for (int i = 0; i < mRenderFinishedSemaphores.mCount; ++i)
    {
        vkDestroySemaphore(mDevice, mRenderFinishedSemaphores.mData[i], nullptr);
    }
    vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
    vkDestroyPipeline(mDevice, mGraphicsPipelineLines, nullptr);
    vkDestroyPipeline(mDevice, mGraphicsPipelineShadow, nullptr);
    vkDestroyPipeline(mDevice, mGraphicsPipeline, nullptr);
    vkDestroyPipelineLayout(mDevice, mShadowPipelineLayout, nullptr);
    vkDestroyPipelineLayout(mDevice, mLinePipelineLayout, nullptr);
    vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
    CleanupSwapchain();
    vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
    vkDestroyDevice(mDevice, nullptr);
    vkDestroyInstance(mInstance, nullptr);
}

void Renderer::UpdateCamera(Vec3 cameraPosition, const Mat4& worldToView)
{
    mUniformData.mCameraPosition = cameraPosition;

    mUniformData.mWorldToView = worldToView;
    mUniformData.mWorldToClip = mUniformData.mViewToClip * worldToView;
}

bool Renderer::StartNewFrame()
{
    Frame& frame = mFrame[mFrameIndex];
    // NOTE: it's illegal to start writing new data to buffers while they are still being used by
    // device, so we are waiting for the "queue submit" fence.
    // TODO: we don't need to wait for vkQueuePresentKHR, this should be enough, right?
    // Citing the spec:
    //     fence is an optional handle to a fence to be signaled once all submitted command
    //     buffers have completed execution
    // TODO: if that's enough, maybe this is over-synchronization?
    gTimeMeters[TimeMeter::NewFrameFence].Start();
    VK_CHECK(vkWaitForFences(mDevice, 1, &frame.mQueueSubmitFence, VK_TRUE, 1'000'000'000));
    gTimeMeters[TimeMeter::NewFrameFence].End();
    VK_CHECK(vkResetFences(mDevice, 1, &frame.mQueueSubmitFence));

    mNewFrameStarted = true;

    mImguiRenderer.StartNewFrame();

    mFrame[mFrameIndex].mDrawIndirectCommandsCount = 0;
    mFrame[mFrameIndex].mDrawDataCount = 0;
    mFrame[mFrameIndex].mLineDataCount = 0;

    return true;
}

void Renderer::RenderScene(VkPipeline pipeline) const
{
    const Frame& frame = mFrame[mFrameIndex];
    const VkCommandBuffer cmd = frame.mCommandBuffer;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindIndexBuffer(
        cmd,
        mVertexIndexBuffer.mBuffer,
        static_cast<VkDeviceSize>(mVerticesCount) * sizeof(Vertex),
        VK_INDEX_TYPE_UINT16
    );
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        mPipelineLayout,
        0,
        1,
        &frame.mDescriptorSet,
        0,
        nullptr
    );

    vkCmdDrawIndexedIndirect(
        cmd,
        frame.mDrawIndirectBuffer.mBuffer,
        0,
        static_cast<u32>(frame.mDrawIndirectCommandsCount),
        sizeof(VkDrawIndexedIndirectCommand)
    );
}

void Renderer::RenderShadowPass() const
{
    const Frame& frame = mFrame[mFrameIndex];
    const VkCommandBuffer cmd = frame.mCommandBuffer;

    VkRenderingAttachmentInfo depthAttachmentInfo{};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = mShadowMapImageViewCascade[0];
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachmentInfo.clearValue = {{{0.0f, 0}}};

    VkRect2D renderArea{};
    renderArea.extent = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = renderArea;
    renderingInfo.layerCount = 1;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;

    {
        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = RENDERER_SHADOW_MAP_CASCADE_COUNT;

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
            | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
            | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = mShadowMapImage.mImage;
        barrier.subresourceRange = subresourceRange;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }

    PushConstants pushConstants{};

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mGraphicsPipelineShadow);
    vkCmdBindIndexBuffer(
        cmd,
        mVertexIndexBuffer.mBuffer,
        static_cast<VkDeviceSize>(mVerticesCount) * sizeof(Vertex),
        VK_INDEX_TYPE_UINT16
    );
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        mPipelineLayout,
        0,
        1,
        &frame.mDescriptorSet,
        0,
        nullptr
    );

    VkViewport viewport{};
    viewport.width = static_cast<f32>(RENDERER_SHADOW_MAP_DIMENSIONS);
    viewport.height = static_cast<f32>(RENDERER_SHADOW_MAP_DIMENSIONS);
    viewport.maxDepth = 1.0f;
    viewport.minDepth = 0.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // TODO: check out VK_KHR_multiview.
    for (uint i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        depthAttachmentInfo.imageView = mShadowMapImageViewCascade[i];
        vkCmdBeginRendering(cmd, &renderingInfo);
        pushConstants.mCascadeIndex = i;
        vkCmdPushConstants(
            cmd,
            mShadowPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(pushConstants),
            &pushConstants
        );

        vkCmdDrawIndexedIndirect(
            cmd,
            frame.mDrawIndirectBuffer.mBuffer,
            0,
            static_cast<u32>(frame.mDrawIndirectCommandsCount),
            sizeof(VkDrawIndexedIndirectCommand)
        );

        vkCmdEndRendering(cmd);
    }
}

bool Renderer::RecordCommandBuffer(u32 imageIndex)
{
    const Frame& frame = mFrame[mFrameIndex];
    const VkCommandBuffer cmd = frame.mCommandBuffer;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo{};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // NOTE: specifying VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT as
    // srcStageMask forms dependency chain with vkQueueSubmit, since
    // waitDstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    // therefore, transitioning image layout will occur only after
    // mImageAcquireSemaphore is signalled.
    Vulkan::ImageMemoryBarrier(
        cmd,
        mSwapchainImages.mData[imageIndex].mImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );

    Vulkan::ImageMemoryBarrier(
        cmd,
        mDepthImage.mImage,
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    );

    Vulkan::ImageMemoryBarrier(
        cmd,
        mRenderImage.mImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );

    RenderShadowPass();

    VkRenderingAttachmentInfo renderingAttachmentInfo{};
    renderingAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    renderingAttachmentInfo.imageView = mRenderImage.mView;
    renderingAttachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    renderingAttachmentInfo.resolveImageView = mSwapchainImages.mData[imageIndex].mView;
    renderingAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    renderingAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    renderingAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    renderingAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const Vec3 clearColor = Colors::SrgbToLinear(Color{60, 60, 120});
    renderingAttachmentInfo.clearValue = {{{clearColor.X(), clearColor.Y(), clearColor.Z(), 1.0f}}};

    VkRenderingAttachmentInfo depthAttachmentInfo{};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = mDepthImage.mView;
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachmentInfo.clearValue = {{{0.0f, 0}}};

    VkRect2D renderArea{};
    renderArea.offset.x = 0;
    renderArea.offset.y = 0;
    renderArea.extent = mSwapchainExtent;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = renderArea;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &renderingAttachmentInfo;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;

    {
        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = RENDERER_SHADOW_MAP_CASCADE_COUNT;

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
            | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = mShadowMapImage.mImage;
        barrier.subresourceRange = subresourceRange;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }

    vkCmdBeginRendering(cmd, &renderingInfo);

    // NOTE: flipping Y here, allowed since VK_KHR_MAINTENANCE_1 (core in 1.1).
    VkViewport viewport{};
    viewport.width = static_cast<f32>(mSwapchainExtent.width);
    viewport.height = -static_cast<f32>(mSwapchainExtent.height);
    viewport.y = static_cast<f32>(mSwapchainExtent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = mSwapchainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    RenderScene(mGraphicsPipeline);

    if (frame.mLineDataCount > 0)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mGraphicsPipelineLines);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            mLinePipelineLayout,
            0,
            1,
            &frame.mLineDescriptorSet,
            0,
            nullptr
        );
        vkCmdDraw(cmd, static_cast<u32>(frame.mLineDataCount) * 2, 1, 0, 0);
    }
    vkCmdEndRendering(cmd);

    if (!mImguiRenderer.UpdateVertexIndexBuffers(static_cast<u32>(mFrameIndex)))
    {
        return false;
    }

    if (mEnableUI)
    {
        renderingAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        renderingAttachmentInfo.imageView = mSwapchainImages.mData[imageIndex].mView;
        renderingAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
        renderingInfo.pDepthAttachment = VK_NULL_HANDLE;
        vkCmdBeginRendering(cmd, &renderingInfo);
        if (!mImguiRenderer.Render(cmd, static_cast<u32>(mFrameIndex)))
        {
            return false;
        }
        vkCmdEndRendering(cmd);
    }

    Vulkan::ImageMemoryBarrier(
        cmd,
        mSwapchainImages.mData[imageIndex].mImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        VK_ACCESS_2_NONE
    );

    VK_CHECK(vkEndCommandBuffer(cmd));

    return true;
}

// github.com/SaschaWillems/Vulkan/blob/master/examples/shadowmappingcascade/shadowmappingcascade.cpp
// https://johanmedestrom.wordpress.com/2016/03/18/opengl-cascaded-shadow-maps/
// https://alextardif.com/shadowmapping.html
// https://www.sebastiansylvan.com/post/improving-shadow-map-utilization-for-cascaded-shadow-maps/
void Renderer::UpdateShadowCascades()
{
    const Mat4 viewToWorld = Inverse(mUniformData.mWorldToView);
    const Mat4 worldToLight = LookAt(Vec3{0.0f}, mUniformData.mLightDirectionWorld, WORLD_Y);

    const f32 coeffY = tanf(FOV_Y_RAD / 2.0f);
    const f32 aspectRatio
        = static_cast<f32>(mSwapchainExtent.width) / static_cast<f32>(mSwapchainExtent.height);
    const f32 coeffX = coeffY * aspectRatio;

    // Calculate a combined view and orthographic projection matrix for each cascade.
    f32 lastSplitDistance = -NEAR_PLANE;
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        const f32 splitDistance = mShadowCascadeSplitDepths[i];

        // clang-format off
        const f32 nearAbsY = lastSplitDistance * coeffY;
        const f32 farAbsY = splitDistance * coeffY;
        const f32 nearAbsX = lastSplitDistance * coeffX;
        const f32 farAbsX = splitDistance * coeffX;

        // clang-format off
        // Directly constructing a frustum in view space.
        Vec3 frustumCorners[] = {
            Vec3{ nearAbsX, -nearAbsY, lastSplitDistance},
            Vec3{ nearAbsX,  nearAbsY, lastSplitDistance},
            Vec3{-nearAbsX,  nearAbsY, lastSplitDistance},
            Vec3{-nearAbsX, -nearAbsY, lastSplitDistance},
            Vec3{ farAbsX,  -farAbsY,  splitDistance},
            Vec3{ farAbsX,   farAbsY,  splitDistance},
            Vec3{-farAbsX,   farAbsY,  splitDistance},
            Vec3{-farAbsX,  -farAbsY,  splitDistance},
        };
        // clang-format on
        constexpr int FRUSTUM_CORNERS_COUNT = ARRAY_SIZE(frustumCorners);
        static_assert(FRUSTUM_CORNERS_COUNT == 8);

        Vec3 frustumCenter{};
        for (int j = 0; j < FRUSTUM_CORNERS_COUNT; ++j)
        {
            frustumCenter += frustumCorners[j];
        }
        frustumCenter *= 1.0f / FRUSTUM_CORNERS_COUNT;

        f32 frustumRadius = -FLT_MAX;
        for (int j = 0; j < FRUSTUM_CORNERS_COUNT; ++j)
        {
            frustumRadius = Max(frustumRadius, MagnitudeSq(frustumCenter - frustumCorners[j]));
        }
        // By using radius instead of min/max points of the frustum, we fix the projection size,
        // since a sphere can enclose any possible frustum orientation.
        // This helps with shadow map stability.
        frustumRadius = sqrtf(frustumRadius);
        const f32 frustumDiameter = frustumRadius * 2.0f;

        const Mat4 worldToLightScaled
            = Scale(worldToLight, RENDERER_SHADOW_MAP_DIMENSIONS / frustumDiameter);
        const Mat4 lightToWorldScaled = Inverse(worldToLightScaled);

        Vec4 sphereCenter = {0.0f, 0.0f, lastSplitDistance - frustumRadius * 0.95f, 1.0f};
        sphereCenter = viewToWorld * sphereCenter;
        // Texel snapping in light space to further stabilize shadow maps.
        sphereCenter = worldToLightScaled * sphereCenter;
        sphereCenter.X() = floorf(sphereCenter.X());
        sphereCenter.Y() = floorf(sphereCenter.Y());
        sphereCenter = lightToWorldScaled * sphereCenter;

        const Mat4 lightViewMatrix = LookAt(
            sphereCenter.XYZ() - mUniformData.mLightDirectionWorld * frustumRadius,
            sphereCenter.XYZ(),
            WORLD_Y
        );

        // (1, 1) is negative to flip Y.
        // clang-format off
        Mat4 lightProjectionMatrix = {
            1.0f / frustumRadius, 0.0f, 0.0f, 0.0f,
            0.0f, -1.0f / frustumRadius, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f / frustumDiameter, 0.0f,
            0.0f, 0.0f, 1.0f, 1.0f
        };
        // clang-format on

        mUniformData.mShadow.mTexelSizes[i] = frustumDiameter / RENDERER_SHADOW_MAP_DIMENSIONS;
        mUniformData.mShadow.mWorldToClip[i] = lightProjectionMatrix * lightViewMatrix;

        lastSplitDistance = splitDistance;
    }
}

bool Renderer::Render()
{
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

    Frame& frame = mFrame[mFrameIndex];

    u32 imageIndex = 0;
    VkResult vulkanResult = vkAcquireNextImageKHR(
        mDevice,
        mSwapchain,
        1'000'000'000,
        frame.mImageAcquireSemaphore,
        nullptr,
        &imageIndex
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

    mUniformData.mLightDirectionView
        = (mUniformData.mWorldToView * Vec4{mUniformData.mLightDirectionWorld, 0.0f}).XYZ();
    mUniformData.mLightDirectionView = Normalize(mUniformData.mLightDirectionView);

    gTimeMeters[TimeMeter::UpdateShadowCascades].Start();
    if (mEnableShadowCascadesUpdate)
    {
        UpdateShadowCascades();
    }
    gTimeMeters[TimeMeter::UpdateShadowCascades].End();

    memcpy(mFrame[mFrameIndex].mUniformDataBuffer.mMapped, &mUniformData, sizeof(mUniformData));

    if (!RecordCommandBuffer(imageIndex))
    {
        return false;
    }

    constexpr VkPipelineStageFlags waitDstStageMask
        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.mImageAcquireSemaphore;
    submitInfo.pWaitDstStageMask = &waitDstStageMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.mCommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &mRenderFinishedSemaphores.mData[imageIndex];
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
    VK_CHECK(vkQueueSubmit(mQueueInfo.mQueue, 1, &submitInfo, frame.mQueueSubmitFence));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &mRenderFinishedSemaphores.mData[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &mSwapchain;
    presentInfo.pImageIndices = &imageIndex;
    vulkanResult = vkQueuePresentKHR(mQueueInfo.mQueue, &presentInfo);
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

    mFrameIndex = (mFrameIndex + 1) % RENDERER_MAX_FRAMES_IN_FLIGHT;

    return true;
}

void Renderer::PauseRendering(bool paused)
{
    mRenderingPaused = paused;
}

void Renderer::SetLightDirection(f32 yaw, f32 pitch)
{
    mUniformData.mLightDirectionWorld.X() = sinf(yaw) * cosf(pitch);
    mUniformData.mLightDirectionWorld.Y() = -sinf(pitch);
    mUniformData.mLightDirectionWorld.Z() = -cosf(yaw) * cosf(pitch);
    assert(AlmostEqual(Magnitude(mUniformData.mLightDirectionWorld), 1.0f));
}

void Renderer::SetLightColor(Vec3 color)
{
    mUniformData.mLightColor = color;
}

void Renderer::EnableShadowCascadesColor(bool enabled)
{
    mUniformData.mEnableShadowCascadesColor = enabled;
}

void Renderer::EnableShadowPcf(bool enabled)
{
    mUniformData.mEnableShadowPCF = enabled;
}

void Renderer::EnableShadowCascadesUpdate(bool enabled)
{
    mEnableShadowCascadesUpdate = enabled;
}

void Renderer::EnableShadowTexelColoring(bool enabled)
{
    mUniformData.mEnableShadowTexelColoring = enabled;
}

void Renderer::EnableUI(bool enabled)
{
    mEnableUI = enabled;
}

void Renderer::ChooseView(u32 number)
{
    assert(number <= RENDERER_SHADOW_MAP_CASCADE_COUNT);
    mUniformData.mPerspectiveChosen = number;
}

const char* Renderer::GetGpuName() const
{
    return mGpuName;
}

void Renderer::DrawModel(
    Vec3 position,
    Quat orientation,
    Vec3 size,
    Color color,
    VkDrawIndexedIndirectCommand drawCommand
)
{
    assert(mNewFrameStarted);
    assert(drawCommand.indexCount > 0);

    Frame& frame = mFrame[mFrameIndex];
    assert(frame.mDrawIndirectCommandsCount < MAX_DRAW_CALLS);
    if (frame.mDrawIndirectCommandsCount >= MAX_DRAW_CALLS)
    {
        return;
    }

    DrawData drawData{};
    drawData.mLocalToWorld = Model(position, orientation, size);
    drawData.mLocalToWorldNormal = Transpose(Inverse(ToMat3(drawData.mLocalToWorld)));
    drawData.mColor = {
        color.mR / 255.0f,
        color.mG / 255.0f,
        color.mB / 255.0f,
    };

    memcpy(
        static_cast<u8*>(frame.mDrawDataBuffer.mMapped)
            + sizeof(drawData) * static_cast<size_t>(frame.mDrawDataCount),
        &drawData,
        sizeof(drawData)
    );
    ++frame.mDrawDataCount;

    memcpy(
        static_cast<u8*>(frame.mDrawIndirectBuffer.mMapped)
            + sizeof(mDrawCommandCube) * static_cast<size_t>(frame.mDrawIndirectCommandsCount),
        &drawCommand,
        sizeof(drawCommand)
    );
    ++frame.mDrawIndirectCommandsCount;
}

void Renderer::DrawBox(Vec3 position, Quat orientation, Vec3 size, Color color)
{
    DrawModel(position, orientation, size, color, mDrawCommandCube);
}

void Renderer::DrawCube(Vec3 position, Quat orientation, f32 size, Color color)
{
    DrawBox(position, orientation, Vec3{size}, color);
}

void Renderer::DrawTetrahedron(Vec3 position, Quat orientation, Vec3 scale, Color color)
{
    DrawModel(position, orientation, scale, color, mDrawCommandTetrahedron);
}

void Renderer::DrawSphere(Vec3 position, Quat orientation, f32 radius, Color color)
{
    DrawModel(position, orientation, Vec3{radius}, color, mDrawCommandSphere);
}

void Renderer::DrawPoint(Vec3 position, f32 radius, Color color)
{
    DrawBox(position, Quat{1.0f, 0.0f, 0.0f, 0.0f}, Vec3{radius}, color);
}

void Renderer::DrawLine(Vec3 point1, Vec3 point2, Color color)
{
    assert(mNewFrameStarted);

    Frame& frame = mFrame[mFrameIndex];
    assert(frame.mDrawIndirectCommandsCount < MAX_DRAW_CALLS);
    if (frame.mDrawIndirectCommandsCount >= MAX_DRAW_CALLS)
    {
        return;
    }

    LineData lineData{};
    lineData.mPosition1 = point1;
    lineData.mPosition2 = point2;
    lineData.mColor1 = PackToF32({
        color.mR / 255.0f,
        color.mG / 255.0f,
        color.mB / 255.0f,
    });
    lineData.mColor2 = lineData.mColor1;

    memcpy(
        static_cast<u8*>(frame.mLineDataBuffer.mMapped)
            + sizeof(lineData) * static_cast<size_t>(frame.mLineDataCount),
        &lineData,
        sizeof(lineData)
    );
    ++frame.mLineDataCount;
}

void Renderer::DrawLineOrigin(Vec3 origin, Vec3 line, Color color)
{
    DrawLine(origin, origin + line, color);
}

bool Renderer::RecreateSwapchain()
{
    VK_CHECK(vkDeviceWaitIdle(mDevice));

    Arena scratch = gArenaReset;

    CleanupSwapchain();
    CleanupColorResources();
    CleanupDepthResources();

    gArenaSwapchain.FreeAll();

    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    VK_CHECK(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysicalDevice, mSurface, &surfaceCapabilities)
    );
    if (surfaceCapabilities.currentExtent.width != UINT32_MAX)
    {
        mSwapchainExtent = surfaceCapabilities.currentExtent;
    }
    else
    {
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSizeInPixels(mWindow, &width, &height))
        {
            fprintf(stderr, "SDL_GetWindowSizeInPixels failed: %s\n", SDL_GetError());
            return false;
        }
        assert(width > 0);
        assert(height > 0);
        mSwapchainExtent.width = Clamp(
            static_cast<u32>(width),
            surfaceCapabilities.minImageExtent.width,
            surfaceCapabilities.maxImageExtent.width
        );
        mSwapchainExtent.height = Clamp(
            static_cast<u32>(height),
            surfaceCapabilities.minImageExtent.height,
            surfaceCapabilities.maxImageExtent.height
        );
    }

    mUniformData.mViewToClip = Perspective(
        FOV_Y_RAD,
        static_cast<f32>(mSwapchainExtent.width) / static_cast<f32>(mSwapchainExtent.height),
        NEAR_PLANE
    );
    mUniformData.mWorldToClip = mUniformData.mViewToClip * mUniformData.mWorldToView;

    u32 surfaceFormatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mSurface, &surfaceFormatCount, nullptr);
    assert(surfaceFormatCount > 0);
    Slice<VkSurfaceFormatKHR> surfaceFormats{};
    surfaceFormats.mCount = static_cast<int>(surfaceFormatCount);
    surfaceFormats.mData = scratch.AllocOrDie<VkSurfaceFormatKHR>(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        mPhysicalDevice,
        mSurface,
        &surfaceFormatCount,
        surfaceFormats.mData
    );

    bool swapchainSurfaceFormatFound = false;
    for (u32 i = 0; i < surfaceFormatCount; ++i)
    {
        const VkFormat format = surfaceFormats.mData[i].format;
        if ((format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB)
            && (surfaceFormats.mData[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
        {
            mSwapchainSurfaceFormat = surfaceFormats.mData[i];
            swapchainSurfaceFormatFound = true;
            break;
        }
    }
    if (!swapchainSurfaceFormatFound)
    {
        fprintf(stderr, "Vulkan failed to find a suitable swapchain surface format\n");
        return false;
    }

    mSwapchainMinImageCount = Max(3u, surfaceCapabilities.minImageCount);
    const u32 maxImageCount = surfaceCapabilities.maxImageCount;
    if (surfaceCapabilities.maxImageCount > 0 && maxImageCount < mSwapchainMinImageCount)
    {
        mSwapchainMinImageCount = maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainCreateInfo{};
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface = mSurface;
    swapchainCreateInfo.minImageCount = mSwapchainMinImageCount;
    swapchainCreateInfo.imageFormat = mSwapchainSurfaceFormat.format;
    swapchainCreateInfo.imageColorSpace = mSwapchainSurfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent = mSwapchainExtent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainCreateInfo.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(mDevice, &swapchainCreateInfo, nullptr, &mSwapchain));

    u32 swapchainImageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(mDevice, mSwapchain, &swapchainImageCount, nullptr));
    VkImage* const images = scratch.AllocOrDie<VkImage>(swapchainImageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(mDevice, mSwapchain, &swapchainImageCount, images));

    mSwapchainImages.mCount = static_cast<int>(swapchainImageCount);
    mSwapchainImages.mData = gArenaSwapchain.AllocOrDie<Vulkan::Image>(swapchainImageCount);
    for (u32 i = 0; i < swapchainImageCount; ++i)
    {
        mSwapchainImages.mData[i].mImage = images[i];
    }

    // Creating image views for every swapchain image.
    VkImageViewCreateInfo imageViewCreateInfo{};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = mSwapchainSurfaceFormat.format;
    imageViewCreateInfo.subresourceRange = {};
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    imageViewCreateInfo.subresourceRange.levelCount = 1;

    for (u32 i = 0; i < swapchainImageCount; ++i)
    {
        imageViewCreateInfo.image = mSwapchainImages.mData[i].mImage;
        VkImageView imageView{};
        VK_CHECK(vkCreateImageView(mDevice, &imageViewCreateInfo, nullptr, &imageView));
        mSwapchainImages.mData[i].mView = imageView;
    }

    if (!CreateDepthResources())
    {
        return false;
    }

    if (!CreateColorResources())
    {
        return false;
    }

    return true;
}

void Renderer::CleanupSwapchain()
{
    for (int i = 0; i < mSwapchainImages.mCount; ++i)
    {
        vkDestroyImageView(mDevice, mSwapchainImages.mData[i].mView, nullptr);
        mSwapchainImages.mData[i].mView = VK_NULL_HANDLE;
    }
    vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
    mSwapchain = VK_NULL_HANDLE;
}

bool Renderer::CreateDepthResources()
{
    VkExtent3D extent{};
    extent.width = mSwapchainExtent.width;
    extent.height = mSwapchainExtent.height;
    extent.depth = 1;

    constexpr VkFormat DEPTH_STENCIL_FORMAT = VK_FORMAT_D32_SFLOAT_S8_UINT;
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, DEPTH_STENCIL_FORMAT, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
    {
        fprintf(stderr, "Vulkan failed to find a suitable depth/stencil format\n");
        return false;
    }
    mDepthFormat = DEPTH_STENCIL_FORMAT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = mDepthFormat;
    imageInfo.extent = extent;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = mMsaaSamples;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(mDevice, &imageInfo, nullptr, &mDepthImage.mImage));

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(mDevice, mDepthImage.mImage, &memoryRequirements);

    u32 memoryTypeIndex = 0;
    const bool result = Vulkan::FindMemoryType(
        memoryTypeIndex,
        mPhysicalDevice,
        memoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    if (!result)
    {
        fprintf(stderr, "Vulkan failed to find a suitable memory type\n");
        return false;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = memoryTypeIndex;
    VK_CHECK(vkAllocateMemory(mDevice, &allocateInfo, nullptr, &mDepthImage.mMemory));

    VK_CHECK(vkBindImageMemory(mDevice, mDepthImage.mImage, mDepthImage.mMemory, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.image = mDepthImage.mImage;
    viewInfo.format = mDepthFormat;
    viewInfo.subresourceRange = {};
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.subresourceRange.levelCount = 1;
    VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mDepthImage.mView));

    return true;
}

void Renderer::CleanupDepthResources()
{
    vkDestroyImageView(mDevice, mDepthImage.mView, nullptr);
    mDepthImage.mView = VK_NULL_HANDLE;
    vkFreeMemory(mDevice, mDepthImage.mMemory, nullptr);
    mDepthImage.mMemory = VK_NULL_HANDLE;
    vkDestroyImage(mDevice, mDepthImage.mImage, nullptr);
    mDepthImage.mImage = VK_NULL_HANDLE;
}

bool Renderer::CreateColorResources()
{
    VkExtent3D extent{};
    extent.width = mSwapchainExtent.width;
    extent.height = mSwapchainExtent.height;
    extent.depth = 1;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = mSwapchainSurfaceFormat.format;
    imageInfo.extent = extent;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = mMsaaSamples;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(mDevice, &imageInfo, nullptr, &mRenderImage.mImage));

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(mDevice, mRenderImage.mImage, &memoryRequirements);

    u32 memoryTypeIndex = 0;
    const bool result = Vulkan::FindMemoryType(
        memoryTypeIndex,
        mPhysicalDevice,
        memoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    if (!result)
    {
        fprintf(stderr, "Vulkan failed to find a suitable memory type\n");
        return false;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = memoryTypeIndex;
    VK_CHECK(vkAllocateMemory(mDevice, &allocateInfo, nullptr, &mRenderImage.mMemory));

    VK_CHECK(vkBindImageMemory(mDevice, mRenderImage.mImage, mRenderImage.mMemory, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.image = mRenderImage.mImage;
    viewInfo.format = mSwapchainSurfaceFormat.format;
    viewInfo.subresourceRange = {};
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.subresourceRange.levelCount = 1;
    VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mRenderImage.mView));

    return true;
}

void Renderer::CleanupColorResources()
{
    vkDestroyImageView(mDevice, mRenderImage.mView, nullptr);
    mRenderImage.mView = VK_NULL_HANDLE;
    vkFreeMemory(mDevice, mRenderImage.mMemory, nullptr);
    mRenderImage.mMemory = VK_NULL_HANDLE;
    vkDestroyImage(mDevice, mRenderImage.mImage, nullptr);
    mRenderImage.mImage = VK_NULL_HANDLE;
}
