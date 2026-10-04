#pragma once

#include "../Arena.hpp"
#include "../Common.hpp"
#include "../Math/Types.hpp"
#include "../Math/Utils.hpp"
#include "ImguiRenderer.hpp"
#include "RHI/RHI.hpp"
#include "Scene.hpp"
#include "Shaders/SharedConfig.hlsli"
#include "Shaders/SharedDef.hlsli"
#include "UniformBufferManager.hpp"

struct SDL_Window;

struct Renderer {
    static constexpr f32 FOV_Y_RAD = Radians(70.0f);
    static constexpr int MAX_DRAW_CALLS = 4096;
    static constexpr u32 MAX_DESCRIPTOR_COUNT = 16384;
    static constexpr int UNIFORM_BUFFER_MAX_SIZE_BYTES = 16384;
    static constexpr int PUSH_CONSTANTS_MAX_SIZE_BYTES = 128;
    // NOTE: can set to 2 to torture my GPU since it's powerful but my monitor is 1080p.
    // TODO: UI toggle?
    static constexpr int RENDER_SCALE = 1;

    static_assert(sizeof(UniformData) <= UNIFORM_BUFFER_MAX_SIZE_BYTES);

    struct Semaphore {
        RHI::Semaphore semaphore;
        u64 value;

        u64 Inc() {
            return ++value;
        }
    };

    struct Frame {
        UniformBufferManager uniformBufferManager;
        Semaphore startSemaphore;
        Semaphore shadowSemaphore;
        Semaphore ssaoSemaphore;
        RHI::CommandBuffer startCommandBuffer;
        RHI::CommandBuffer shadowCommandBuffer;
        RHI::CommandBuffer ssaoCommandBuffer;
        RHI::CommandBuffer endCommandBuffer;
        RHI::Texture resolvedRenderTexture;
    };

    Arena mScratchArena;
    SDL_Window* mWindow;
    RHI::DescriptorInfo mUniformBufferDescriptor;
    RHI::DescriptorInfo mShadowPassUniformBufferDescriptor;
    RHI::Semaphore mFrameSemaphore;
    RHI::Texture mVisibilityTexture;
    RHI::Texture mRenderTexture;
    RHI::Texture mVelocityTexture;
    RHI::Texture mAmbientOcclusionTexture;
    RHI::Texture mAmbientOcclusionBlurredHorizontalTexture;
    RHI::Texture mAmbientOcclusionBlurredVerticalTexture;
    RHI::Texture mAmbientOcclusionUpsampledTexture;
    RHI::Texture mShadowTexture;
    RHI::Texture mShadowPcfJitterTexture;
    RHI::Texture mFogTexture;
    RHI::Texture mFogBlurredHorizontalTexture;
    RHI::Texture mFogBlurredVerticalTexture;
    RHI::TextureDescriptor mShadowTextureDescriptorCascade[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    RHI::Texture mDepthTexture;
    RHI::Texture mDepthViewQuarterResTexture;
    RHI::Texture mDepthPyramidTexture;
    std::vector<RHI::TextureDescriptor> mDepthPyramidMipTextureDescriptors;
    RHI::Pipeline mVisibilityPipeline;
    RHI::Pipeline mDepthViewQuarterResPipeline;
    RHI::Pipeline mAmbientOcclusionPipeline;
    RHI::Pipeline mAmbientOcclusionBlurPipeline;
    RHI::Pipeline mAmbientOcclusionUpsamplePipeline;
    RHI::Pipeline mFogPipeline;
    RHI::Pipeline mBlurFogPipeline;
    RHI::Pipeline mShadowCullPipeline;
    RHI::Pipeline mShadowPipeline;
    RHI::Pipeline mVisibilityRenderPipeline;
    RHI::Pipeline mFullscreenPipeline;
    RHI::Pipeline mCullEarlyPipeline;
    RHI::Pipeline mCullLatePipeline;
    RHI::Pipeline mTaaResolvePipeline;
    RHI::Pipeline mDebugGradErrorPipeline;
    RHI::Pipeline mDepthReducePipeline;
    RHI::Pipeline mDebugDrawRectPipeline;
    RHI::Pipeline mDebugDrawFillCmdPipeline;
    RHI::Buffer mVertexBuffer;
    RHI::Buffer mIndexBuffer;
    RHI::Buffer mDrawCmdBuffer1;
    RHI::Buffer mDrawCmdEarlyBuffer2;
    RHI::Buffer mDrawCmdLateBuffer2;
    RHI::Buffer mDrawCmdShadowBuffer;
    RHI::Buffer mDrawIndicesEarlyBuffer;
    RHI::Buffer mDrawIndicesLateBuffer;
    RHI::Buffer mDrawIndicesShadowBuffer;
    RHI::Buffer mMaterialBuffer;
    RHI::Buffer mDrawDataBuffer;
    RHI::Buffer mDrawCountBuffer;
    RHI::Buffer mMeshPrimitiveVisibleBuffer;
    RHI::Buffer mDebugDrawCountBuffer;
    RHI::Buffer mDebugDrawRectBuffer;
    RHI::Buffer mDebugDrawCmdBuffer;
    ImguiRenderer mImguiRenderer;
    RHI::Sampler mTextureSampler;
    RHI::Sampler mLinearSampler;
    RHI::Sampler mNearestSampler;
    RHI::Sampler mMinSampler;
    RHI::Sampler mShadowSampler;
    RHI::Sampler mShadowPcfJitterSampler;
    std::vector<RHI::Texture> mTextures;
    U32Vec2 mWindowSize;
    Frame mFrame[RHI::FRAMES_IN_FLIGHT];
    f32 mShadowCascadeRadii[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    int mFrameIdx;
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

    void VisibilityBufferPass(RHI::CommandBuffer cb, bool cullLate);
    void CullPass(RHI::CommandBuffer cb, bool late);
    void DepthReducePass(RHI::CommandBuffer cb);

    void DepthViewQuarterResPass(RHI::CommandBuffer cb);
    void AmbientOcclusionPass(RHI::CommandBuffer cb);
    void AmbientOcclusionBlurPass(RHI::CommandBuffer cb, bool horizontal);
    void AmbientOcclusionUpsamplePass(RHI::CommandBuffer cb);

    void ShadowCullPass(RHI::CommandBuffer cb);
    void ShadowPass(RHI::CommandBuffer cb);

    void FogPass(RHI::CommandBuffer cb);
    void BlurFogPass(RHI::CommandBuffer cb, bool horizontal);

    void RenderPass(RHI::CommandBuffer cb);
    void TaaResolvePass(RHI::CommandBuffer cb);
    void DebugDrawPass(RHI::CommandBuffer cb);
    void FullscreenPass(RHI::CommandBuffer cb, RHI::Texture swapchainTexture);

    void
    DebugDrawGradErrorPass(RHI::CommandBuffer cb, bool cullLate, RHI::Texture swapchainTexture);

    void RecordAndSubmitDebugGradError(RHI::Texture swapchainTexture);
    void RecordAndSubmitVisibility(RHI::Texture swapchainTexture);
    void CreateSwapchain(U32Vec2 size);
    void CleanupSwapchain();
    void CreateColorResources();
    void CleanupColorResources();
    void CreateDepthResources();
    void CleanupDepthResources();
    void CleanupPipelines();
};
