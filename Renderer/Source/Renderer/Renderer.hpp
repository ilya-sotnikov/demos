#pragma once

#include "../Common.hpp"
#include "../Arena.hpp"
#include "RHI/RHI.hpp"
#include "ImguiRenderer.hpp"
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
    static constexpr int PUSH_CONSTANTS_MAX_SIZE_BYTES = 128;
    // NOTE: can set to 2 to torture my GPU since it's powerful but my monitor is 1080p.
    // TODO: UI toggle?
    static constexpr int RENDER_SCALE = 1;

    static_assert(sizeof(UniformData) <= UNIFORM_BUFFER_MAX_SIZE_BYTES);

    struct Semaphore
    {
        RHI::SemaphoreHandle semaphore;
        u64 value;

        u64 Inc()
        {
            return ++value;
        }
    };

    struct Frame
    {
        Semaphore startSemaphore;
        Semaphore shadowSemaphore;
        Semaphore ssaoSemaphore;
        RHI::CommandBufferHandle startCommandBuffer;
        RHI::CommandBufferHandle shadowCommandBuffer;
        RHI::CommandBufferHandle ssaoCommandBuffer;
        RHI::CommandBufferHandle endCommandBuffer;
        RHI::BufferHandle uniformBuffer;
        RHI::TextureHandle resolvedRenderTexture;
    };

    Arena mScratchArena;
    SDL_Window* mWindow;
    RHI::SemaphoreHandle mFrameSemaphore;
    RHI::TextureHandle mVisibilityTexture;
    RHI::TextureHandle mRenderTexture;
    RHI::TextureHandle mVelocityTexture;
    RHI::TextureHandle mAmbientOcclusionTexture;
    RHI::TextureHandle mAmbientOcclusionBlurredHorizontalTexture;
    RHI::TextureHandle mAmbientOcclusionBlurredVerticalTexture;
    RHI::TextureHandle mAmbientOcclusionUpsampledTexture;
    RHI::TextureHandle mShadowTexture;
    RHI::TextureHandle mShadowPcfJitterTexture;
    RHI::TextureHandle mFogTexture;
    RHI::TextureHandle mFogBlurredHorizontalTexture;
    RHI::TextureHandle mFogBlurredVerticalTexture;
    RHI::TextureDescriptorHandle mShadowTextureDescriptorCascade[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    RHI::TextureHandle mDepthTexture;
    RHI::TextureHandle mDepthViewQuarterResTexture;
    RHI::TextureHandle mDepthPyramidTexture;
    std::vector<RHI::TextureDescriptorHandle> mDepthPyramidMipTextureDescriptors;
    RHI::PipelineHandle mVisibilityPipeline;
    RHI::PipelineHandle mDepthViewQuarterResPipeline;
    RHI::PipelineHandle mAmbientOcclusionPipeline;
    RHI::PipelineHandle mAmbientOcclusionBlurPipeline;
    RHI::PipelineHandle mAmbientOcclusionUpsamplePipeline;
    RHI::PipelineHandle mFogPipeline;
    RHI::PipelineHandle mBlurFogPipeline;
    RHI::PipelineHandle mShadowCullPipeline;
    RHI::PipelineHandle mShadowPipeline;
    RHI::PipelineHandle mVisibilityRenderPipeline;
    RHI::PipelineHandle mFullscreenPipeline;
    RHI::PipelineHandle mCullEarlyPipeline;
    RHI::PipelineHandle mCullLatePipeline;
    RHI::PipelineHandle mTaaResolvePipeline;
    RHI::PipelineHandle mDebugGradErrorPipeline;
    RHI::PipelineHandle mDepthReducePipeline;
    RHI::PipelineHandle mDebugDrawRectPipeline;
    RHI::PipelineHandle mDebugDrawFillCmdPipeline;
    RHI::BufferHandle mVertexBuffer;
    RHI::BufferHandle mIndexBuffer;
    RHI::BufferHandle mDrawCmdBuffer1;
    RHI::BufferHandle mDrawCmdEarlyBuffer2;
    RHI::BufferHandle mDrawCmdLateBuffer2;
    RHI::BufferHandle mDrawCmdShadowBuffer;
    RHI::BufferHandle mDrawIndicesEarlyBuffer;
    RHI::BufferHandle mDrawIndicesLateBuffer;
    RHI::BufferHandle mDrawIndicesShadowBuffer;
    RHI::BufferHandle mMaterialBuffer;
    RHI::BufferHandle mDrawDataBuffer;
    RHI::BufferHandle mDrawCountBuffer;
    RHI::BufferHandle mMeshPrimitiveVisibleBuffer;
    RHI::BufferHandle mDebugDrawCountBuffer;
    RHI::BufferHandle mDebugDrawRectBuffer;
    RHI::BufferHandle mDebugDrawCmdBuffer;
    ImguiRenderer mImguiRenderer;
    RHI::SamplerHandle mTextureSampler;
    RHI::SamplerHandle mLinearSampler;
    RHI::SamplerHandle mNearestSampler;
    RHI::SamplerHandle mMinSampler;
    RHI::SamplerHandle mShadowSampler;
    RHI::SamplerHandle mShadowPcfJitterSampler;
    std::vector<RHI::TextureHandle> mTextures;
    U32Vec2 mWindowSize;
    Frame mFrame[RHI::FRAMES_IN_FLIGHT];
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
    void SetSunDirection(f32 yaw, f32 pitch);

private:
    bool UploadTextures(const std::vector<std::string>& texturePaths);
    void UpdateShadowCascades();

    void VisibilityBufferPass(RHI::CommandBufferHandle cb, bool cullLate);
    void CullPass(RHI::CommandBufferHandle cb, bool late);
    void DepthReducePass(RHI::CommandBufferHandle cb);

    void DepthViewQuarterResPass(RHI::CommandBufferHandle cb);
    void AmbientOcclusionPass(RHI::CommandBufferHandle cb);
    void AmbientOcclusionBlurPass(RHI::CommandBufferHandle cb, bool horizontal);
    void AmbientOcclusionUpsamplePass(RHI::CommandBufferHandle cb);

    void ShadowCullPass(RHI::CommandBufferHandle cb);
    void ShadowPass(RHI::CommandBufferHandle cb);

    void FogPass(RHI::CommandBufferHandle cb);
    void BlurFogPass(RHI::CommandBufferHandle cb, bool horizontal);

    void RenderPass(RHI::CommandBufferHandle cb);
    void TaaResolvePass(RHI::CommandBufferHandle cb);
    void DebugDrawPass(RHI::CommandBufferHandle cb);
    void FullscreenPass(RHI::CommandBufferHandle cb, RHI::TextureHandle swapchainTexture);

    void DebugDrawGradErrorPass(
        RHI::CommandBufferHandle cb,
        bool cullLate,
        RHI::TextureHandle swapchainTexture
    );

    bool RecordAndSubmitDebugGradError(RHI::TextureHandle swapchainTexture);
    bool RecordAndSubmitVisibility(RHI::TextureHandle swapchainTexture);
    bool CreateSwapchain(U32Vec2 size);
    void CleanupSwapchain();
    bool CreateColorResources();
    void CleanupColorResources();
    bool CreateDepthResources();
    void CleanupDepthResources();
    void CleanupPipelines();
};
