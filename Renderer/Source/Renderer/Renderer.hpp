#pragma once

#include "RendererCommon.hpp"
#include "ImguiRenderer.hpp"
#include "Vulkan.hpp"
#include "Scene.hpp"
#include "../Math/Types.hpp"
#include "../Math/Utils.hpp"
#include "Shaders/SharedConfig.hlsli"
#include "Shaders/SharedDef.hlsli"

struct SDL_Window;

struct Renderer
{
    static constexpr f32 FOV_Y_RAD = Radians(70.0f);
    static constexpr int MAX_DRAW_CALLS = 4096;
    static constexpr u32 MAX_DESCRIPTOR_COUNT = 16384;
    static constexpr int UNIFORM_BUFFER_MAX_SIZE_BYTES = 16384;
    static constexpr int PUSH_CONSTANTS_MAX_SIZE_BYTES = 16384;
    // NOTE: can set to 2 to torture my GPU since it's powerful but my monitor is 1080p.
    // TODO: UI toggle?
    static constexpr int RENDER_SCALE = 1;

    static_assert(sizeof(UniformData) <= UNIFORM_BUFFER_MAX_SIZE_BYTES);
    static_assert(sizeof(PushConstantsImgui) <= PUSH_CONSTANTS_MAX_SIZE_BYTES);
    static_assert(sizeof(PushConstantsVisibilityBuffer) <= PUSH_CONSTANTS_MAX_SIZE_BYTES);
    static_assert(sizeof(PushConstantsDepthReduce) <= PUSH_CONSTANTS_MAX_SIZE_BYTES);
    static_assert(sizeof(PushConstantsShadow) <= PUSH_CONSTANTS_MAX_SIZE_BYTES);

    struct Frame
    {
        VkCommandBuffer commandBufferStart;
        VkCommandBuffer commandBufferShadow;
        VkCommandBuffer commandBufferEnd;
        VkCommandBuffer commandBufferSSAO;
        VkFence fenceQueueSubmit;
        VkSemaphore semaphoreImageAcquire;
        Vulkan::TimelineSemaphore semaphoreStart;
        Vulkan::TimelineSemaphore semaphoreShadow;
        Vulkan::TimelineSemaphore semaphoreSSAO;
        Vulkan::Buffer uniformBuffer;
        Vulkan::Image resolvedRenderImage;
    };

    SDL_Window* mWindow;
    Vulkan::Device mDevice;
    VkSurfaceKHR mSurface;
    Vulkan::Swapchain mSwapchain;
    Vulkan::Image mVisibilityImage;
    Vulkan::Image mRenderImage;
    Vulkan::Image mVelocityImage;
    Vulkan::Image mAmbientOcclusionImage;
    Vulkan::Image mAmbientOcclusionBlurredImage;
    Vulkan::Image mShadowImage;
    Vulkan::Image mShadowPcfJitterImage;
    VkImageView mShadowImageViewCascade[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    VkExtent2D mRenderImageExtent;
    VkExtent2D mAmbientOcclusionImageExtent;
    VkExtent2D mDepthPyramidImageExtent;
    Vulkan::Image mDepthImage;
    Vulkan::Image mDepthPyramidImage;
    std::vector<VkImageView> mDepthPyramidMipImageViews;
    Vulkan::Pipeline mVisibilityPipeline;
    Vulkan::Pipeline mAmbientOcclusionPipeline;
    Vulkan::Pipeline mAmbientOcclusionBlurPipeline;
    Vulkan::Pipeline mShadowCullPipeline;
    Vulkan::Pipeline mShadowPipeline;
    Vulkan::Pipeline mVisibilityRenderPipeline;
    Vulkan::Pipeline mForwardRenderPipeline;
    Vulkan::Pipeline mFullscreenPipeline;
    Vulkan::Pipeline mCullEarlyPipeline;
    Vulkan::Pipeline mCullLatePipeline;
    Vulkan::Pipeline mTaaResolvePipeline;
    Vulkan::Pipeline mDebugGradErrorPipeline;
    Vulkan::Pipeline mDepthReducePipeline;
    Vulkan::Pipeline mDebugDrawRectPipeline;
    Vulkan::Pipeline mDebugDrawFillCmdPipeline;
    Vulkan::Buffer mVertexBuffer;
    Vulkan::Buffer mIndexBuffer;
    Vulkan::Buffer mDrawCmdBuffer1;
    Vulkan::Buffer mDrawCmdEarlyBuffer2;
    Vulkan::Buffer mDrawCmdLateBuffer2;
    Vulkan::Buffer mDrawCmdShadowBuffer;
    Vulkan::Buffer mDrawIndicesEarlyBuffer;
    Vulkan::Buffer mDrawIndicesLateBuffer;
    Vulkan::Buffer mDrawIndicesShadowBuffer;
    Vulkan::Buffer mMaterialBuffer;
    Vulkan::Buffer mDrawDataBuffer;
    Vulkan::Buffer mDrawCountBuffer;
    Vulkan::Buffer mMeshPrimitiveVisibleBuffer;
    Vulkan::Buffer mDebugDrawCountBuffer;
    Vulkan::Buffer mDebugDrawRectBuffer;
    Vulkan::Buffer mDebugDrawCmdBuffer;
    ImguiRenderer mImguiRenderer;
    VkDescriptorPool mDescriptorPool;
    VkDescriptorSet mTextureDescriptorSet;
    VkDescriptorSetLayout mTextureDescriptorSetLayout;
    VkSampler mTextureSampler;
    VkSampler mLinearSampler;
    VkSampler mNearestSampler;
    VkSampler mMinSampler;
    VkSampler mShadowSampler;
    VkSampler mShadowPcfJitterSampler;
    VkCommandPool mCommandPoolGraphics;
    VkCommandPool mCommandPoolCompute;
    VkSampleCountFlagBits mSampleCount;
    std::vector<VkSemaphore> mRenderFinishedSemaphores;
    std::vector<Vulkan::Image> mTextures;
    Frame mFrame[RENDERER_MAX_FRAMES_IN_FLIGHT];
    f32 mShadowCascadeRadii[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    int mFrameIdx;
    int mPrevFrameIdx;
    u32 mTaaJitterIdx;
    u32 mTaaJitterMaxIdx;
    UniformData mUniformData;
    bool mNewFrameStarted;
    bool mRenderingPaused;
    bool mSwapchainNeedsRecreating;
    bool mSwapchainRecreated;
    bool mEnableUI;
    bool mRenderModeChanged;
    bool mCullCameraFrozen;

    bool Init();
    void Cleanup();
    bool StartNewFrame();
    bool Render(f32 deltaTime);
    void UpdateCamera(Vec3 position, const Mat4& worldToView);
    void PauseRendering(bool paused);
    void ChangeRenderMode(RenderMode mode);
    void FreezeCullCamera(bool frozen);
    bool RecompilePipelines();

private:
    bool UploadTextures(const std::vector<std::string>& texturePaths);
    void UpdateShadowCascades();

    void VisibilityBufferPass(VkCommandBuffer cb, bool cullLate);
    void ForwardPass(VkCommandBuffer cb, bool cullLate);
    void CullPass(VkCommandBuffer cb, bool late);
    void DepthReducePass(VkCommandBuffer cb);
    void AmbientOcclusionPass(VkCommandBuffer cb);
    void AmbientOcclusionBlurPass(VkCommandBuffer cb);
    void ShadowCullPass(VkCommandBuffer cb);
    void ShadowPass(VkCommandBuffer cb);
    void RenderPass(VkCommandBuffer cb);
    void TaaResolvePass(VkCommandBuffer cb);
    void DebugDrawPass(VkCommandBuffer cb);
    void FullscreenPass(VkCommandBuffer cb, u32 imageIdx);

    void DebugDrawGradErrorPass(VkCommandBuffer cb, bool cullLate, u32 imageIdx);

    bool RecordCommandBufferDebugGradError(u32 imageIdx);
    bool RecordCommandBufferVisibility(u32 imageIdx);
    // NOTE: forward renderer is implemented as a reference, to help with
    // debugging visibility buffer, I won't implement every feature here
    // though, sice the main brittle points are attribute interpolation
    // and analytic derivatives.
    bool RecordCommandBufferForward(u32 imageIdx);
    bool CreateSwapchain();
    void CleanupSwapchain();
    bool CreateColorResources();
    void CleanupColorResources();
    bool CreateDepthResources();
    void CleanupDepthResources();
    void CleanupPipelines();
};
