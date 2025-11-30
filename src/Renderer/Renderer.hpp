#pragma once

#include "Common.hpp"
#include "ImGuiRenderer.hpp"
#include "Shaders/SharedConstants.slang"
#include "../Math/Types.hpp"
#include "../Math/Utils.hpp"
#include "../Colors.hpp"

struct SDL_Window;

struct Renderer
{
    bool Init(SDL_Window* window);
    void Cleanup();
    void UpdateCamera(Vec3 cameraPosition, const Mat4& worldToView);
    bool StartNewFrame();
    bool Render();
    void PauseRendering(bool paused);
    void SetLightDirection(f32 yaw, f32 pitch);
    void SetLightColor(Vec3 color);
    void EnableShadowCascadesColor(bool enabled);
    void EnableShadowPcf(bool enabled);
    void EnableShadowCascadesUpdate(bool enabled);
    void EnableShadowTexelColoring(bool enabled);
    void EnableUI(bool enabled);
    void ChooseView(u32 number);
    const char* GetGpuName() const;

    void DrawBox(Vec3 position, Quat orientation, Vec3 size, Color color);
    void DrawCube(Vec3 position, Quat orientation, f32 size, Color color);
    void DrawTetrahedron(Vec3 position, Quat orientation, Vec3 scale, Color color);
    void DrawSphere(Vec3 position, Quat orientation, f32 radius, Color color);
    void DrawPoint(Vec3 position, f32 radius, Color color);
    void DrawLine(Vec3 point1, Vec3 point2, Color color);
    void DrawLineOrigin(Vec3 origin, Vec3 line, Color color);

private:
    static constexpr f32 FOV_Y_RAD = Radians(60.0f);
    static constexpr f32 NEAR_PLANE = 0.1f;
    static constexpr f32 SHADOW_FAR_PLANE = 200.0f;
    static constexpr int MAX_DRAW_CALLS = 8192;
    static constexpr f32 SHADOW_MAP_CASCADE_SPLIT_LAMBDA = 0.85f;

    struct Vertex
    {
        Vec3 mPosition;
        Vec3 mNormal;
    };

    struct UniformData
    {
        Mat4 mWorldToClip;
        Mat4 mWorldToView;
        Mat4 mViewToClip;
        Vec3 mCameraPosition;
        Vec3 mLightDirectionWorld;
        Vec3 mLightDirectionView;
        Vec3 mLightColor;
        uint mEnableShadowCascadesColor;
        uint mEnableShadowPCF;
        uint mEnableShadowTexelColoring;
        uint mPerspectiveChosen;

        struct Shadow
        {
            f32 mTexelSizes[RENDERER_SHADOW_MAP_CASCADE_COUNT];
            Mat4 mWorldToClip[RENDERER_SHADOW_MAP_CASCADE_COUNT];
        } mShadow;
    };

    struct DrawData
    {
        Mat4 mLocalToWorld;
        Mat3 mLocalToWorldNormal; // (M^-1)^T
        Vec3 mColor;
    };

    struct LineData
    {
        Vec3 mPosition1;
        f32 mColor1;
        Vec3 mPosition2;
        f32 mColor2;
    };

    struct PushConstants
    {
        uint mCascadeIndex;
    };

    SDL_Window* mWindow;
    ImGuiRenderer mImguiRenderer;
    int mVerticesCount;
    int mIndicesCount;
    VkInstance mInstance;
    VkPhysicalDevice mPhysicalDevice;
    VkDevice mDevice;
    Vulkan::QueueInfo mQueueInfo;
    VkSurfaceKHR mSurface;
    VkSurfaceFormatKHR mSwapchainSurfaceFormat;
    VkFormat mDepthFormat;
    VkFormat mShadowDepthFormat;
    VkExtent2D mSwapchainExtent;
    u32 mSwapchainMinImageCount;
    Slice<Vulkan::Image> mSwapchainImages;
    Vulkan::Image mDepthImage;
    Vulkan::SampledImage mShadowMapImage;
    VkImageView mShadowMapImageViewCascade[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    VkSwapchainKHR mSwapchain;
    VkPipelineLayout mPipelineLayout;
    VkPipelineLayout mLinePipelineLayout;
    VkPipelineLayout mShadowPipelineLayout;
    VkPipeline mGraphicsPipeline;
    VkPipeline mGraphicsPipelineLines;
    VkPipeline mGraphicsPipelineShadow;
    VkCommandPool mCommandPool;
    // TODO: check out VK_EXT_swapchain_maintenance1.
    Slice<VkSemaphore> mRenderFinishedSemaphores;
    Vulkan::Buffer mVertexIndexBuffer;
    VkDescriptorSetLayout mDescriptorSetLayout;
    VkDescriptorSetLayout mLineDescriptorSetLayout;
    VkDescriptorPool mDescriptorPool;
    VkDrawIndexedIndirectCommand mDrawCommandCube;
    VkDrawIndexedIndirectCommand mDrawCommandSphere;
    VkDrawIndexedIndirectCommand mDrawCommandTetrahedron;
    VkSampleCountFlagBits mMsaaSamples;
    Vulkan::Image mRenderImage;
    Vulkan::SampledImage mShadowJitterOffsetsImage;
    f32 mShadowCascadeSplitDepths[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    bool mEnableShadowCascadesUpdate;
    bool mEnableUI;
    char mGpuName[256];

    struct Frame
    {
        VkCommandBuffer mCommandBuffer;
        VkFence mQueueSubmitFence;
        VkSemaphore mImageAcquireSemaphore;
        VkDescriptorSet mDescriptorSet;
        VkDescriptorSet mLineDescriptorSet;

        Vulkan::Buffer mUniformDataBuffer;

        Vulkan::Buffer mDrawDataBuffer;
        int mDrawDataCount;

        Vulkan::Buffer mLineDataBuffer;
        int mLineDataCount;

        Vulkan::Buffer mDrawIndirectBuffer;
        int mDrawIndirectCommandsCount;
    } mFrame[RENDERER_MAX_FRAMES_IN_FLIGHT];

    int mFrameIndex;
    bool mRenderingPaused;
    bool mSwapchainNeedsRecreating;
    bool mNewFrameStarted;

    UniformData mUniformData;

    void DrawModel(
        Vec3 position,
        Quat orientation,
        Vec3 size,
        Color color,
        VkDrawIndexedIndirectCommand drawCommand
    );
    void RenderScene(VkPipeline pipeline) const;
    void RenderShadowPass() const;
    bool RecordCommandBuffer(u32 imageIndex);
    void UpdateShadowCascades();
    bool RecreateSwapchain();
    void CleanupSwapchain();
    bool CreateDepthResources();
    void CleanupDepthResources();
    bool CreateColorResources();
    void CleanupColorResources();
};

// clang-format off
inline constexpr Color gColorSequence[] = {
    {255, 0, 0},
    {0, 255, 0},
    {0, 0, 255},
    {255, 255, 0},
    {255, 0, 255},
    {127, 127, 127}
};
// clang-format on
inline int gColorSequenceCount = ARRAY_SIZE(gColorSequence);

inline Renderer gRenderer;
