#pragma once

#include "arena.hpp"
#include "common.hpp"
#include "imgui_renderer.hpp"
#include "math/types.hpp"
#include "math/utils.hpp"
#include "rhi/rhi.hpp"
#include "scene.hpp"
#include "shaders/shared_config.hlsli"
#include "shaders/shared_def.hlsli"
#include "uniform_buffer_manager.hpp"

struct SDL_Window;

struct Renderer
{
    static constexpr f32 FOV_Y_RAD = Radians(70.0f);
    static constexpr int MAX_DRAW_CALLS = 4096;
    static constexpr u32 MAX_DESCRIPTOR_COUNT = 16384;
    static constexpr int UNIFORM_BUFFER_MAX_SIZE_BYTES = 16384;
    static constexpr int PUSH_CONSTANTS_MAX_SIZE_BYTES = 128;
    // NOTE: can set to 2 to torture my GPU since it's powerful but my monitor is 1080p.
    // TODO: UI toggle?
    static constexpr int RENDER_SCALE = 1;

    static_assert(sizeof(UniformData) <= UNIFORM_BUFFER_MAX_SIZE_BYTES);

    struct Semaphore
    {
        rhi::Semaphore semaphore;
        u64 value;

        u64 Inc()
        {
            return ++value;
        }
    };

    struct Frame
    {
        UniformBufferManager uniformBufferManager;
        Semaphore startSemaphore;
        Semaphore shadowSemaphore;
        Semaphore ssaoSemaphore;
        rhi::CommandPool graphics_command_pool;
        rhi::CommandPool compute_command_pool;
        rhi::CommandBuffer startCommandBuffer;
        rhi::CommandBuffer shadowCommandBuffer;
        rhi::CommandBuffer ssaoCommandBuffer;
        rhi::CommandBuffer endCommandBuffer;
        rhi::Texture resolvedRenderTexture;
    };

    Arena mScratchArena;
    SDL_Window* mWindow;
    rhi::Queue m_graphics_queue;
    rhi::Queue m_compute_queue;
    rhi::DescriptorInfo mUniformBufferDescriptor;
    rhi::DescriptorInfo mShadowPassUniformBufferDescriptor;
    rhi::Semaphore mFrameSemaphore;
    rhi::Texture mVisibilityTexture;
    rhi::Texture mRenderTexture;
    rhi::Texture mVelocityTexture;
    rhi::Texture mAmbientOcclusionTexture;
    rhi::Texture mAmbientOcclusionBlurredHorizontalTexture;
    rhi::Texture mAmbientOcclusionBlurredVerticalTexture;
    rhi::Texture mAmbientOcclusionUpsampledTexture;
    rhi::Texture mShadowTexture;
    rhi::Texture mShadowPcfJitterTexture;
    rhi::Texture mFogTexture;
    rhi::Texture mFogBlurredHorizontalTexture;
    rhi::Texture mFogBlurredVerticalTexture;
    rhi::TextureDescriptor mShadowTextureDescriptorCascade[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    rhi::Texture mDepthTexture;
    rhi::Texture mDepthViewQuarterResTexture;
    rhi::Texture mDepthPyramidTexture;
    std::vector<rhi::TextureDescriptor> mDepthPyramidMipTextureDescriptors;
    rhi::Pipeline mVisibilityPipeline;
    rhi::Pipeline mDepthViewQuarterResPipeline;
    rhi::Pipeline mAmbientOcclusionPipeline;
    rhi::Pipeline mAmbientOcclusionBlurPipeline;
    rhi::Pipeline mAmbientOcclusionUpsamplePipeline;
    rhi::Pipeline mFogPipeline;
    rhi::Pipeline mBlurFogPipeline;
    rhi::Pipeline mShadowCullPipeline;
    rhi::Pipeline mShadowPipeline;
    rhi::Pipeline mVisibilityRenderPipeline;
    rhi::Pipeline mFullscreenPipeline;
    rhi::Pipeline mCullEarlyPipeline;
    rhi::Pipeline mCullLatePipeline;
    rhi::Pipeline mTaaResolvePipeline;
    rhi::Pipeline mDebugGradErrorPipeline;
    rhi::Pipeline mDepthReducePipeline;
    rhi::Pipeline mDebugDrawRectPipeline;
    rhi::Pipeline mDebugDrawFillCmdPipeline;
    rhi::Buffer mVertexBuffer;
    rhi::Buffer mIndexBuffer;
    rhi::Buffer mDrawCmdBuffer1;
    rhi::Buffer mDrawCmdEarlyBuffer2;
    rhi::Buffer mDrawCmdLateBuffer2;
    rhi::Buffer mDrawCmdShadowBuffer;
    rhi::Buffer mDrawIndicesEarlyBuffer;
    rhi::Buffer mDrawIndicesLateBuffer;
    rhi::Buffer mDrawIndicesShadowBuffer;
    rhi::Buffer mMaterialBuffer;
    rhi::Buffer mDrawDataBuffer;
    rhi::Buffer mDrawCountBuffer;
    rhi::Buffer mMeshPrimitiveVisibleBuffer;
    rhi::Buffer mDebugDrawCountBuffer;
    rhi::Buffer mDebugDrawRectBuffer;
    rhi::Buffer mDebugDrawCmdBuffer;
    ImguiRenderer mImguiRenderer;
    rhi::Sampler mTextureSampler;
    rhi::Sampler mLinearSampler;
    rhi::Sampler mNearestSampler;
    rhi::Sampler mMinSampler;
    rhi::Sampler mShadowSampler;
    rhi::Sampler mShadowPcfJitterSampler;
    std::vector<rhi::Texture> mTextures;
    Vec2U32 mWindowSize;
    Frame m_frame[rhi::FRAMES_IN_FLIGHT];
    f32 mShadowCascadeRadii[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    int m_frame_idx;
    int mPrevFrameIdx;
    u32 mTaaJitterIdx;
    u32 mTaaJitterMaxIdx;
    UniformData mUniformData;
    ShadowPassData mShadowPassData;
    PushConstantsCull mCullPassData;
    PushConstantsSSAO mSsaoPassData;
    PushConstantsTAA mTaaPassData;
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
    void SetSunDirection(f32 yaw, f32 pitch);

private:
    bool UploadTextures(const std::vector<std::string>& texturePaths);
    void UpdateShadowCascades();

    void VisibilityBufferPass(rhi::CommandBuffer cb, bool cullLate);
    void CullPass(rhi::CommandBuffer cb, bool late);
    void DepthReducePass(rhi::CommandBuffer cb);

    void DepthViewQuarterResPass(rhi::CommandBuffer cb);
    void AmbientOcclusionPass(rhi::CommandBuffer cb);
    void AmbientOcclusionBlurPass(rhi::CommandBuffer cb, bool horizontal);
    void AmbientOcclusionUpsamplePass(rhi::CommandBuffer cb);

    void ShadowCullPass(rhi::CommandBuffer cb);
    void ShadowPass(rhi::CommandBuffer cb);

    void FogPass(rhi::CommandBuffer cb);
    void BlurFogPass(rhi::CommandBuffer cb, bool horizontal);

    void RenderPass(rhi::CommandBuffer cb);
    void TaaResolvePass(rhi::CommandBuffer cb);
    void DebugDrawPass(rhi::CommandBuffer cb);
    void FullscreenPass(rhi::CommandBuffer cb, rhi::Texture swapchainTexture);

    void DebugDrawGradErrorPass(
        rhi::CommandBuffer cb,
        bool cullLate,
        rhi::Texture swapchainTexture
    );

    void RecordAndSubmitDebugGradError(rhi::Texture swapchainTexture);
    void RecordAndSubmitVisibility(rhi::Texture swapchainTexture);
    void CreateSwapchain(Vec2U32 size);
    void CleanupSwapchain();
    void CreateColorResources();
    void CleanupColorResources();
    void CreateDepthResources();
    void CleanupDepthResources();
    void CleanupPipelines();
};
