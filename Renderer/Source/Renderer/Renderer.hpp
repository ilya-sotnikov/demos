#pragma once

#include "RendererCommon.hpp"
#include "ImguiRenderer.hpp"
#include "Vulkan.hpp"
#include "Scene.hpp"
#include "../Math/Types.hpp"
#include "../Math/Utils.hpp"
#include "Shaders/SharedConfig.slang"
#include "Shaders/SharedDef.slang"

struct SDL_Window;

struct Renderer
{
    static constexpr f32 FOV_Y_RAD = Radians(70.0f);
    static constexpr int MAX_DRAW_CALLS = 4096;
    static constexpr u32 MAX_DESCRIPTOR_COUNT = 16384;
    static constexpr int UNIFORM_BUFFER_MAX_SIZE_BYTES = 16384;
    // NOTE: can set to 2 to torture my GPU since it's powerful but my monitor is 1080p.
    // TODO: UI toggle?
    static constexpr int RENDER_SCALE = 1;

    static_assert(sizeof(UniformData) <= UNIFORM_BUFFER_MAX_SIZE_BYTES);

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
    VmaAllocator mVmaAllocator;
    VkInstance mInstance;
    VkSurfaceKHR mSurface;
    VkPhysicalDevice mPhysicalDevice;
    VkDevice mDevice;
    Vulkan::QueueInfo mQueueInfo;
    Vulkan::Swapchain mSwapchain;
    Vulkan::Image mVisibilityImage;
    Vulkan::Image mRenderImage;
    Vulkan::Image mVelocityImage;
    Vulkan::Image mGradImage;
    VkExtent2D mRenderImageExtent;
    Vulkan::Image mDepthImage;
    Vulkan::Pipeline mVisibilityPipeline;
    Vulkan::Pipeline mRenderPipeline;
    Vulkan::Pipeline mFullscreenPipeline;
    Vulkan::Pipeline mCullPipeline;
    Vulkan::Pipeline mTaaResolvePipeline;
    Vulkan::Pipeline mDebugGradErrorPipeline;
    Vulkan::Buffer mVertexBuffer;
    Vulkan::Buffer mIndexBuffer;
    Vulkan::Buffer mIndirectBuffer1;
    Vulkan::Buffer mIndirectBuffer2;
    Vulkan::Buffer mDrawIndicesBuffer;
    Vulkan::Buffer mMaterialBuffer;
    Vulkan::Buffer mDrawDataBuffer;
    Vulkan::Buffer mIndirectCountBuffer;
    Vulkan::Buffer mBlasBuffer;
    Vulkan::Buffer mTlasBuffer;
    std::vector<VkAccelerationStructureKHR> mBlas;
    VkAccelerationStructureKHR mTlas;
    ImguiRenderer mImguiRenderer;
    VkDescriptorPool mDescriptorPool;
    VkDescriptorSet mTextureDescriptorSet;
    VkDescriptorSetLayout mTextureDescriptorSetLayout;
    VkSampler mTextureSampler;
    VkSampler mLinearSampler;
    VkFormat mVisibilityImageFormat;
    VkFormat mGradImageFormat;
    VkFormat mDepthFormat;
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
    char mGpuName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    bool mNewFrameStarted;
    bool mRenderingPaused;
    bool mSwapchainNeedsRecreating;
    bool mSwapchainRecreated;
    bool mEnableUI;
    bool mRenderModeChanged;
    RenderMode mRenderMode;

    bool Init();
    void Cleanup();
    bool StartNewFrame();
    bool Render(f32 deltaTime);
    void UpdateCamera(Vec3 position, const Mat4& worldToView);
    void PauseRendering(bool paused);
    void ChangeRenderMode(RenderMode mode);

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

    void VisibilityBufferPass(VkCommandBuffer cmd);
    void CullPass(VkCommandBuffer cmd);
    void RenderPass(VkCommandBuffer cmd);
    void TaaResolvePass(VkCommandBuffer cmd);
    void FullscreenPass(VkCommandBuffer cmd, u32 imageIdx);

    bool RecordDebugGradErrorCommandBuffer(u32 imageIdx);
    bool RecordCommandBuffer(u32 imageIdx);
    bool CreateSwapchain();
    void CleanupSwapchain();
    bool CreateColorResources();
    void CleanupColorResources();
    bool CreateDepthResources();
    void CleanupDepthResources();
};
