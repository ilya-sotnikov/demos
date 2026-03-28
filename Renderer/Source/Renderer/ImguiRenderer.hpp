#pragma once

#include "RendererCommon.hpp"
#include "Vulkan.hpp"
#include "Shaders/SharedConfig.slang"
#include "../Math/Types.hpp"
#include "Shaders/SharedDef.slang"

#include "../Math/Types.hpp"

struct SDL_Window;

struct ImguiRenderer
{
    struct PushConstantBlock
    {
        Vec2 scale;
        Vec2 translate;
    };

    // From main renderer.
    VkDevice mDevice;
    VkPhysicalDevice mPhysicalDevice;
    VkCommandPool mCommandPool;
    Vulkan::QueueInfo mQueueInfo;
    VmaAllocator mVmaAllocator;

    // It's own resources.
    Vulkan::Image mFontImage;
    VkSampler mFontSampler;
    Vulkan::Pipeline mPipeline;

    struct Frame
    {
        Vulkan::Buffer vertexBuffer;
        Vulkan::Buffer indexBuffer;
        VkDeviceSize vertexBufferSize;
        VkDeviceSize indexBufferSize;
        int vertexCount;
        int indexCount;
    } mFrame[RENDERER_MAX_FRAMES_IN_FLIGHT];

    bool Init(
        SDL_Window* window,
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VmaAllocator vmaAllocator,
        VkCommandPool commandPool,
        Vulkan::QueueInfo queueInfo,
        VkFormat colorFormat
    );
    void Cleanup();
    bool UpdateVertexIndexBuffers(u32 frameIndex);
    void StartNewFrame() const;
    bool Render(VkCommandBuffer cmd, u32 frameIndex);
};
