#pragma once

#include "Common.hpp"
#include "Vulkan.hpp"
#include "../Math/Types.hpp"
#include "Shaders/SharedConstants.slang"

struct SDL_Window;

struct ImGuiRenderer
{
    struct PushConstantBlock
    {
        Vec2 mScale;
        Vec2 mTranslate;
    };

    // From main renderer.
    VkDevice mDevice;
    VkPhysicalDevice mPhysicalDevice;
    VkCommandPool mCommandPool;
    Vulkan::QueueInfo mQueueInfo;

    // It's own resources.
    Vulkan::SampledImage mFontImage;
    VkDescriptorPool mDescriptorPool;
    VkDescriptorSet mDescriptorSet;
    VkPipelineLayout mPipelineLayout;
    PushConstantBlock mPushConstantBlock;
    VkPipeline mPipeline;

    struct Frame
    {
        Vulkan::Buffer mVertexBuffer;
        Vulkan::Buffer mIndexBuffer;
        VkDeviceSize mVertexBufferSize;
        VkDeviceSize mIndexBufferSize;
        int mVertexCount;
        int mIndexCount;
    } mFrame[RENDERER_MAX_FRAMES_IN_FLIGHT];

    bool Init(
        SDL_Window* window,
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkCommandPool commandPool,
        Vulkan::QueueInfo queueInfo,
        VkFormat colorFormat,
        VkFormat depthFormat
    );
    void Cleanup();
    bool UpdateVertexIndexBuffers(u32 frameIndex);
    void StartNewFrame() const;
    bool Render(VkCommandBuffer cmd, u32 frameIndex);
};
