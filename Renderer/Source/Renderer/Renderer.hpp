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

    enum class RenderMode
    {
        Normal,
        GradError,
    };

    struct Frame
    {
        VkCommandBuffer commandBuffer;
        VkFence queueSubmitFence;
        VkSemaphore imageAcquireSemaphore;
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
    VkExtent2D mRenderImageExtent;
    VkExtent2D mDepthPyramidImageExtent;
    Vulkan::Image mDepthImage;
    Vulkan::Image mDepthPyramidImage;
    std::vector<VkImageView> mDepthPyramidMipImageViews;
    Vulkan::Pipeline mVisibilityPipeline;
    Vulkan::Pipeline mRenderPipeline;
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
    Vulkan::Buffer mDrawIndicesEarlyBuffer;
    Vulkan::Buffer mDrawIndicesLateBuffer;
    Vulkan::Buffer mMaterialBuffer;
    Vulkan::Buffer mDrawDataBuffer;
    Vulkan::Buffer mDrawCountBuffer;
    Vulkan::Buffer mBlasBuffer;
    Vulkan::Buffer mTlasBuffer;
    Vulkan::Buffer mMeshPrimitiveVisibleBuffer;
    Vulkan::Buffer mDebugDrawCountBuffer;
    Vulkan::Buffer mDebugDrawRectBuffer;
    Vulkan::Buffer mDebugDrawCmdBuffer;
    std::vector<VkAccelerationStructureKHR> mBlas;
    VkAccelerationStructureKHR mTlas;
    ImguiRenderer mImguiRenderer;
    VkDescriptorPool mDescriptorPool;
    VkDescriptorSet mTextureDescriptorSet;
    VkDescriptorSetLayout mTextureDescriptorSetLayout;
    VkSampler mTextureSampler;
    VkSampler mLinearSampler;
    VkSampler mMinSampler;
    VkCommandPool mCommandPool;
    VkSampleCountFlagBits mSampleCount;
    std::vector<VkSemaphore> mRenderFinishedSemaphores;
    std::vector<Vulkan::Image> mTextures;
    Frame mFrame[RENDERER_MAX_FRAMES_IN_FLIGHT];
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
    RenderMode mRenderMode;

    bool Init();
    void Cleanup();
    bool StartNewFrame();
    bool Render(f32 deltaTime);
    void UpdateCamera(Vec3 position, const Mat4& worldToView);
    void PauseRendering(bool paused);
    void ChangeRenderMode(RenderMode mode);
    void FreezeCullCamera(bool frozen);

private:
    bool UploadTextures(const std::vector<std::string>& texturePaths);
    bool CreateAndUploadBlas(
        const std::vector<MeshPrimitive>& meshPrimitives,
        const std::vector<VkDrawIndexedIndirectCommand>& drawCmds
    );
    bool CreateAndUploadTlas(
        const std::vector<MeshPrimitive>& meshPrimitives,
        const std::vector<DrawData>& drawData
    );

    void VisibilityBufferPass(VkCommandBuffer cmd, bool cullLate);
    void CullPass(VkCommandBuffer cmd, bool late);
    void DepthReducePass(VkCommandBuffer cmd);
    void RenderPass(VkCommandBuffer cmd);
    void TaaResolvePass(VkCommandBuffer cmd);
    void DebugDrawPass(VkCommandBuffer cmd);
    void FullscreenPass(VkCommandBuffer cmd, u32 imageIdx);

    void DebugDrawGradErrorPass(VkCommandBuffer cmd, bool cullLate, u32 imageIdx);

    bool RecordDebugGradErrorCommandBuffer(u32 imageIdx);
    bool RecordCommandBuffer(u32 imageIdx);
    bool CreateSwapchain();
    void CleanupSwapchain();
    bool CreateColorResources();
    void CleanupColorResources();
    bool CreateDepthResources();
    void CleanupDepthResources();
};
