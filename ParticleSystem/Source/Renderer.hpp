#pragma once

#include "Common.hpp"

#include "Vulkan.hpp"
#include "Math/Types.hpp"
#include "Math/Utils.hpp"
#include "Shaders/SharedConfig.slang"
#include "Shaders/SharedDef.slang"

struct SDL_Window;

struct Renderer
{
    static constexpr f32 FOV_Y_RAD = Radians(90.0f);
    static constexpr f32 NEAR_PLANE = 0.1f;

    static constexpr int UNIFORM_BUFFER_MAX_SIZE_BYTES = 16384;

    static_assert(sizeof(UniformData) <= UNIFORM_BUFFER_MAX_SIZE_BYTES);
    static_assert(sizeof(ParticleCountData) <= UNIFORM_BUFFER_MAX_SIZE_BYTES);

    struct Frame
    {
        VkCommandBuffer commandBuffer;
        VkFence queueSubmitFence;
        VkSemaphore imageAcquireSemaphore;
        Vulkan::Buffer uniformBuffer;
        Vulkan::DescriptorBindingInfo descriptorBindingInfos[DESCRIPTOR_BINDING_COUNT];
        VkWriteDescriptorSet writeDescriptorSets[DESCRIPTOR_BINDING_COUNT];
    };

    struct ParticleIndirectData
    {
        u32 dispatchCount[3];
        VkDrawIndexedIndirectCommand drawCmd;
    };

    SDL_Window* mWindow;
    VkInstance mInstance;
    VkSurfaceKHR mSurface;
    VkPhysicalDevice mPhysicalDevice;
    VkDevice mDevice;
    Vulkan::QueueInfo mQueueInfo;
    Vulkan::Swapchain mSwapchain;
    Vulkan::SampledImage mRenderImage;
    VkExtent2D mRenderImageExtent;
    Vulkan::Pipeline mRenderPipeline;
    Vulkan::Pipeline mFullscreenPipeline;
    Vulkan::Pipeline mSimulationPipeline;
    Vulkan::Pipeline mInitPipeline;
    Vulkan::Pipeline mBeforeSimulationPipeline;
    Vulkan::Pipeline mEmitterPipeline;
    Vulkan::Pipeline mAfterSimulationPipeline;
    Vulkan::Buffer mParticleCountBuffer;
    Vulkan::Buffer mParticleCountUniformBuffer;
    Vulkan::Buffer mVelocityMassInvBuffer;
    Vulkan::Buffer mPositionDistToForceFieldInvBuffer;
    Vulkan::Buffer mRadiusBuffer;
    Vulkan::Buffer mTimeLeftBuffer;
    Vulkan::Buffer mDeadBuffer;
    Vulkan::Buffer mAliveBuffer1;
    Vulkan::Buffer mAliveBuffer2;
    Vulkan::Buffer mEmitterIdxBuffer;
    Vulkan::Buffer mEmitterBuffer;
    Vulkan::Buffer mIndirectBuffer;
    VkCommandPool mCommandPool;
    std::vector<VkSemaphore> mRenderFinishedSemaphores;
    Frame mFrame[RENDERER_MAX_FRAMES_IN_FLIGHT];
    int mFrameIdx;
    UniformData mUniformData;
    Emitter mEmitters[MAX_EMITTERS];
    char mGpuName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    bool mNeedResetParticleSystem;
    bool mNewFrameStarted;
    bool mRenderingPaused;
    bool mSwapchainNeedsRecreating;

    bool Init();
    void Cleanup();
    bool StartNewFrame();
    bool Render(f32 deltaTime);
    void UpdateCamera(const Mat4& worldToView);
    void PauseRendering(bool paused);
    void ResetParticleSystem();

    bool RecordCommandBuffer(u32 imageIdx);
    bool RecreateSwapchain();
    void CleanupSwapchain();
    bool CreateColorResources();
    void CleanupColorResources();
};
