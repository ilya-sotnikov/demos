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
    // NOTE: torturing my GPU since it's powerful but my monitor is 1080p.
    static constexpr int RENDER_SCALE = 2;

    static_assert(sizeof(UniformData) <= UNIFORM_BUFFER_MAX_SIZE_BYTES);
    static_assert(sizeof(PushConstants) <= 128);

    struct Frame
    {
        VkCommandBuffer commandBuffer;
        VkFence queueSubmitFence;
        VkSemaphore imageAcquireSemaphore;
        Vulkan::Buffer uniformBuffer;
        Vulkan::DescriptorBindingInfo descriptorBindingInfos[DESCRIPTOR_BINDING_COUNT];
        PushConstants pushConstants;
    };

    SDL_Window* mWindow;
    VmaAllocator mVmaAllocator;
    VkInstance mInstance;
    VkSurfaceKHR mSurface;
    VkPhysicalDevice mPhysicalDevice;
    VkDevice mDevice;
    Vulkan::QueueInfo mQueueInfo;
    Vulkan::Swapchain mSwapchain;
    Vulkan::SampledImage mRenderImage;
    Vulkan::SampledImage mAlbedoImage;
    Vulkan::SampledImage mShadowImage;
    VkExtent2D mRenderImageExtent;
    Vulkan::SampledImage mDepthImage;
    Vulkan::Pipeline mRenderPipeline;
    Vulkan::Pipeline mFullscreenPipeline;
    Vulkan::Pipeline mDepthPrepassPipeline;
    Vulkan::Pipeline mCullPipeline;
    Vulkan::Pipeline mShadowPipeline;
    Vulkan::Buffer mVertexBuffer;
    Vulkan::Buffer mIndexBuffer;
    Vulkan::Buffer mIndirectBuffer1;
    Vulkan::Buffer mIndirectBuffer2;
    Vulkan::Buffer mMaterialBuffer;
    Vulkan::Buffer mDrawDataBuffer;
    Vulkan::Buffer mBlasBuffer;
    Vulkan::Buffer mTlasBuffer;
    Vulkan::Buffer mIndirectCountBuffer;
    std::vector<VkAccelerationStructureKHR> mBlas;
    VkAccelerationStructureKHR mTlas;
    ImguiRenderer mImguiRenderer;
    VkDescriptorPool mDescriptorPool;
    VkDescriptorSet mDescriptorSet;
    VkDescriptorSetLayout mDescriptorSetLayout;
    VkSampler mTextureSampler;
    VkFormat mRenderImageFormat{};
    VkFormat mAlbedoImageFormat{};
    VkFormat mDepthFormat;
    VkCommandPool mCommandPool;
    VkSampleCountFlagBits mSampleCount;
    std::vector<VkSemaphore> mRenderFinishedSemaphores;
    std::vector<Vulkan::Image> mTextures;
    Frame mFrame[RENDERER_MAX_FRAMES_IN_FLIGHT];
    int mFrameIdx;
    UniformData mUniformData;
    char mGpuName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    bool mNewFrameStarted;
    bool mRenderingPaused;
    bool mSwapchainNeedsRecreating;
    bool mEnableUI;

    bool Init();
    void Cleanup();
    bool StartNewFrame();
    bool Render(f32 deltaTime);
    void UpdateCamera(Vec3 position, const Mat4& worldToView);
    void PauseRendering(bool paused);

    bool UploadTextures(const std::vector<std::string>& texturePaths);
    bool CreateAndUploadBlas(
        const std::vector<MeshPrimitive>& meshPrimitives,
        const std::vector<VkDrawIndexedIndirectCommand>& drawCmds
    );
    bool CreateAndUploadTlas(
        const std::vector<MeshPrimitive>& meshPrimitives,
        const std::vector<DrawData>& drawData
    );

    void CullPrepass(VkCommandBuffer cmd);
    void DepthPrepass(VkCommandBuffer cmd);
    void MainPass(VkCommandBuffer cmd);
    void ShadowPass(VkCommandBuffer cmd);
    void FullscreenPass(VkCommandBuffer cmd, u32 imageIdx);

    bool RecordCommandBuffer(u32 imageIdx);
    bool CreateSwapchain();
    void CleanupSwapchain();
    bool CreateColorResources();
    void CleanupColorResources();
    bool CreateDepthResources();
    void CleanupDepthResources();
};
