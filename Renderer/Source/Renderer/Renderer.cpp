#include "Renderer.hpp"

#include "../Utils.hpp"
#include "../Math/Vec2.hpp"
#include "../Math/Mat4.hpp"
#include "RHI/Vulkan/TypeConvert.hpp"

#include <stdio.h>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cgltf.h>
#include <ktx.h>
#include <ktxvulkan.h>

#define SDL_PRINT_ERROR(functionName) \
    fprintf(stderr, "%s:%d: " functionName " failed: %s\n", __FILE__, __LINE__, SDL_GetError())

static u32 CalcDispatchSize(u32 size, u32 localSize)
{
    DEBUG_ASSERT(size > 0);
    DEBUG_ASSERT(localSize > 0);

    return (size + localSize - 1) / localSize;
}

static U32Vec3 CalcDispatchSize(U32Vec2 textureSize, U32Vec3 localSize)
{
    DEBUG_ASSERT(textureSize.x > 0);
    DEBUG_ASSERT(textureSize.y > 0);
    DEBUG_ASSERT(localSize.x > 0);
    DEBUG_ASSERT(localSize.y > 0);
    DEBUG_ASSERT(localSize.z > 0);

    return {
        (textureSize.x + localSize.x - 1) / localSize.x,
        (textureSize.y + localSize.y - 1) / localSize.y,
        1
    };
}

static void CmdDispatchOverTextureSize(
    RHI::CommandBufferHandle cb,
    RHI::PipelineHandle pipeline,
    RHI::TextureHandle texture
)
{
    RHI::CmdDispatch(
        cb,
        CalcDispatchSize(
            RHI::GetTextureDimensions(texture).XY(),
            RHI::GetPipelineLocalSize(pipeline)
        )
    );
}

[[maybe_unused]]
static void CmdFullBarrier(RHI::CommandBufferHandle cb)
{
    RHI::CmdBarrier(
        cb,
        RHI::STAGE_ALL_COMMANDS_BIT,
        RHI::ACCESS_MEMORY_WRITE_BIT,
        RHI::STAGE_ALL_COMMANDS_BIT,
        RHI::ACCESS_MEMORY_READ_BIT | RHI::ACCESS_MEMORY_WRITE_BIT
    );
}

// GPU gems 2, Chapter 17, Efficient Soft-Edged Shadows Using Pixel Shader Branching, Yury Uralsky.
static std::vector<i8> CreateShadowJitterOffsets(size_t size, size_t samplesU, size_t samplesV)
{
    u32 randomU32 = 1337;

    std::vector<i8> result(size * size * samplesU * samplesV * 4 / 2);

    const size_t gridSize = samplesU * samplesV / 2;

    for (size_t i = 0; i < size; ++i)
    {
        for (size_t j = 0; j < size; ++j)
        {
            for (size_t k = 0; k < gridSize; ++k)
            {
                const size_t x = k % (samplesU / 2);
                const size_t y = (samplesV - 1) - k / (samplesU / 2);

                // Generate points on a regular rectangular grid size samplesU * samplesV.
                Vec4 gridPoints{};
                gridPoints[0] = (static_cast<f32>(x) * 2.0f + 0.5f) / static_cast<f32>(samplesU);
                gridPoints[1] = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(samplesV);
                gridPoints[2]
                    = (static_cast<f32>(x) * 2.0f + 1.0f + 0.5f) / static_cast<f32>(samplesU);
                gridPoints[3] = gridPoints[1];

                // Jitter position.
                gridPoints[0] += LfsrNextGetFloat(randomU32, 0.5f / static_cast<f32>(samplesU));
                gridPoints[1] += LfsrNextGetFloat(randomU32, 0.5f / static_cast<f32>(samplesV));
                gridPoints[2] += LfsrNextGetFloat(randomU32, 0.5f / static_cast<f32>(samplesU));
                gridPoints[3] += LfsrNextGetFloat(randomU32, 0.5f / static_cast<f32>(samplesV));

                // Warp jittered rectangular grid to disk.
                const Vec4 diskPoints = {
                    sqrtf(gridPoints[1]) * cosf(M_PIf * 2.0f * gridPoints[0]),
                    sqrtf(gridPoints[1]) * sinf(M_PIf * 2.0f * gridPoints[0]),
                    sqrtf(gridPoints[3]) * cosf(M_PIf * 2.0f * gridPoints[2]),
                    sqrtf(gridPoints[3]) * sinf(M_PIf * 2.0f * gridPoints[2]),
                };

                result[(k * size * size + j * size + i) * 4 + 0] = i8(diskPoints[0] * 127.0f);
                result[(k * size * size + j * size + i) * 4 + 1] = i8(diskPoints[1] * 127.0f);
                result[(k * size * size + j * size + i) * 4 + 2] = i8(diskPoints[2] * 127.0f);
                result[(k * size * size + j * size + i) * 4 + 3] = i8(diskPoints[3] * 127.0f);
            }
        }
    }

    return result;
}

bool Renderer::Init()
{
    mScratchArena.Init(16'000'000);

    // SDL.
    {
#ifdef __linux__
        if (!SDL_SetHint("SDL_VIDEO_DRIVER", "x11"))
        {
            fprintf(
                stderr,
                "WARNING: I've had some strange bugs on Wayland, including hangs, segfaults on "
                "cleanup (glfw), also RenderDoc doesn't work on Wayland. It seems that using "
                "X11 "
                "(or Xwayland) is better for now.\n"
            );
            SDL_PRINT_ERROR("SDL_SetHint(\"SDL_VIDEO_DRIVER\", \"x11\"");
            return false;
        }
#endif

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            SDL_PRINT_ERROR("SDL_InitSubSystem");
            return false;
        }

        VK_CHECK(volkInitialize());

        mWindow = SDL_CreateWindow(
            "renderer",
            800,
            600,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MOUSE_GRABBED
                | SDL_WINDOW_MOUSE_RELATIVE_MODE | SDL_WINDOW_FULLSCREEN
        );
        if (!mWindow)
        {
            SDL_PRINT_ERROR("SDL_CreateWindow");
            return false;
        }

        (void)SDL_SetWindowPosition(mWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        (void)SDL_SetWindowRelativeMouseMode(mWindow, true);
    }

    if (!RHI::Create(mWindow))
    {
        return false;
    }

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(mWindow, &width, &height))
    {
        SDL_PRINT_ERROR("SDL_GetWindowSizeInPixels");
        return false;
    }

    if (!CreateSwapchain({u32(width), u32(height)}))
    {
        return false;
    }

    // Buffers.
    {
        for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
        {
            // NOTE: creating a host visible, coherent, device local buffer.
            // Should be always legal even on discrete GPUs if total allocated
            // size is less than 200 MB or so. But I don't care about the size,
            // since resizable BAR is somewhat widely supported.
            mFrame[i].uniformBuffer = RHI::CreateBuffer({
                .type = RHI::MEMORY_TYPE_DEFAULT_UNIFORM,
                .size = sizeof(UniformData),
                .debugName = "UniformBuffer",
            });
            if (!mFrame[i].uniformBuffer)
            {
                return false;
            }
        }

        mDrawCountBuffer = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32),
            .debugName = "DrawCountBuffer",
        });
        if (!mDrawCountBuffer)
        {
            return false;
        }

        mMeshPrimitiveVisibleBuffer = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debugName = "MeshPrimitiveVisibleBuffer",
        });
        if (!mMeshPrimitiveVisibleBuffer)
        {
            return false;
        }

        mDebugDrawCountBuffer = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * 1, // TODO: maybe enum max count for offsets?
            .debugName = "DebugDrawCountBuffer",
        });
        if (!mDebugDrawCountBuffer)
        {
            return false;
        }

        mDebugDrawRectBuffer = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(DebugDrawRectData) * RENDERER_DEBUG_DRAW_RECT_MAX_COUNT,
            .debugName = "DebugDrawRectBuffer",
        });
        if (!mDebugDrawRectBuffer)
        {
            return false;
        }

        mDebugDrawCmdBuffer = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(RHI::DrawIndirectCommand),
            .debugName = "DebugDrawCmdBuffer",
        });
        if (!mDebugDrawCmdBuffer)
        {
            return false;
        }
    }

    mShadowTexture = RHI::CreateTexture({
        .format = RHI::FORMAT_D16_UNORM,
        .type = RHI::TEXTURE_TYPE_2D_ARRAY,
        .dimensions = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS, 1},
        .layerCount = RENDERER_SHADOW_MAP_CASCADE_COUNT,
        .usage = RHI::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "ShadowTexture",
    });
    if (!mShadowTexture)
    {
        return false;
    }

    if (!RecompilePipelines())
    {
        return false;
    }

    // Synchronization primitives.
    {
        mFrameSemaphore = RHI::CreateSemaphore(0);
        if (!mFrameSemaphore)
        {
            return false;
        }

        for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
        {
            mFrame[i].startSemaphore.semaphore = RHI::CreateSemaphore(0);
            if (!mFrame[i].startSemaphore.semaphore)
            {
                return false;
            }
            mFrame[i].shadowSemaphore.semaphore = RHI::CreateSemaphore(0);
            if (!mFrame[i].shadowSemaphore.semaphore)
            {
                return false;
            }
            mFrame[i].ssaoSemaphore.semaphore = RHI::CreateSemaphore(0);
            if (!mFrame[i].ssaoSemaphore.semaphore)
            {
                return false;
            }
        }
    }

    // Shadow map resources.
    {
        // One texture descriptor per cascade.
        for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
        {
            mShadowTextureDescriptorCascade[i] = RHI::CreateTextureDescriptor({
                .textureHandle = mShadowTexture,
                .type = RHI::TEXTURE_TYPE_2D,
                .baseLayer = u32(i),
                .layerCount = 1,
            });
            if (!mShadowTextureDescriptorCascade[i])
            {
                return false;
            }
        }

        mShadowSampler = RHI::CreateSampler({
            .compareEnable = true,
            .compareOp = RHI::COMPARE_OP_GREATER,
        });
        if (!mShadowSampler)
        {
            return false;
        }

        // PCF jitter offsets.
        {
            mShadowPcfJitterTexture = RHI::CreateTexture({
                .format = RHI::FORMAT_R8G8B8A8_SNORM,
                .type = RHI::TEXTURE_TYPE_3D,
                .dimensions = {
                    RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
                    RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
                    RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U
                        * RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V / 2,
                },
                .usage = RHI::TEXTURE_USAGE_TRANSFER_DST_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
                .debugName = "ShadowPcfJitterTexture",
            });
            if (!mShadowPcfJitterTexture)
            {
                return false;
            }

            const std::vector<i8> jitterOffsets = CreateShadowJitterOffsets(
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U,
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V
            );

            const u64 uploadSize = VEC_SIZE_BYTES(jitterOffsets);
            RHI::BufferHandle stagingBuffer = RHI::CreateBuffer({
                .size = uploadSize,
                .debugName = "StagingBuffer",
            });
            if (!stagingBuffer)
            {
                return false;
            }
            DEFER(RHI::DestroyBuffer(stagingBuffer));

            memcpy(RHI::GetBufferHostPtr(stagingBuffer), jitterOffsets.data(), uploadSize);

            RHI::CommandBufferHandle cb = RHI::CreateCommandBuffer(RHI::QUEUE_GRAPHICS);
            if (!cb)
            {
                return false;
            }
            DEFER(RHI::DestroyCommandBuffer(cb));

            if (!RHI::BeginCommandBuffer(cb))
            {
                return false;
            }

            RHI::CmdTextureBarrier(
                cb,
                {{
                    .handle = mShadowPcfJitterTexture,
                    .oldLayout = RHI::TEXTURE_LAYOUT_UNDEFINED,
                    .newLayout = RHI::TEXTURE_LAYOUT_GENERAL,
                    .srcStageMask = RHI::STAGE_HOST_BIT,
                    .dstStageMask = RHI::STAGE_ALL_TRANSFER_BIT,
                    .dstAccessMask = RHI::ACCESS_TRANSFER_WRITE_BIT,
                }}
            );

            RHI::CmdCopyBufferToTexture(
                cb,
                stagingBuffer,
                mShadowPcfJitterTexture,
                {{
                    .textureDimensions = {
                        u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE),
                        u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE),
                        u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U
                            * RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V / 2),
                    },
                }}
            );

            RHI::CmdBarrier(
                cb,
                RHI::STAGE_ALL_TRANSFER_BIT,
                RHI::ACCESS_TRANSFER_WRITE_BIT,
                RHI::STAGE_FRAGMENT_SHADER_BIT,
                RHI::ACCESS_SHADER_READ_BIT
            );

            if (!RHI::EndCommandBuffer(cb))
            {
                return false;
            }

            if (!RHI::QueueSubmit(RHI::QUEUE_GRAPHICS, {{.cb = cb}}))
            {
                return false;
            }

            (void)RHI::DeviceWaitIdle();

            mShadowPcfJitterSampler = RHI::CreateSampler({
                .magFilter = RHI::FILTER_NEAREST,
                .minFilter = RHI::FILTER_NEAREST,
                .mipmapMode = RHI::SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = RHI::SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = RHI::SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = RHI::SAMPLER_ADDRESS_MODE_REPEAT,
            });
            if (!mShadowPcfJitterSampler)
            {
                return false;
            }
        }
    }

    // Samplers.
    {
        mTextureSampler = RHI::CreateSampler({
            .addressModeU = RHI::SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = RHI::SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = RHI::SAMPLER_ADDRESS_MODE_REPEAT,
            .anisotropyEnable = true,
            .maxAnisotropy = 4.0f,
            .maxLod = 16.0f,
        });
        if (!mTextureSampler)
        {
            return false;
        }

        mLinearSampler = RHI::CreateSampler({});
        if (!mLinearSampler)
        {
            return false;
        }

        mNearestSampler = RHI::CreateSampler({
            .magFilter = RHI::FILTER_NEAREST,
            .minFilter = RHI::FILTER_NEAREST,
        });
        if (!mNearestSampler)
        {
            return false;
        }

        mMinSampler = RHI::CreateSampler({
            .reductionMode = RHI::SAMPLER_REDUCTION_MODE_MIN,
            .maxLod = 16.0f,
        });
        if (!mMinSampler)
        {
            return false;
        }
    }

    // Scene.
    {
        const std::string gltfPath = "../Assets/main_sponza/NewSponza_Main_glTF_003.gltf";

        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        std::vector<MeshPrimitive> meshPrimitives;
        std::vector<RHI::DrawIndexedIndirectCommand> drawCmds;
        std::vector<DrawData> drawData;
        std::vector<Material> materials;
        std::vector<std::string> texturePaths;

        if (!LoadScene(
                vertices,
                indices,
                meshPrimitives,
                drawCmds,
                drawData,
                materials,
                texturePaths,
                mUniformData.sunDirectionWorld,
                gltfPath
            ))
        {
            fprintf(stderr, "scene loading failed, path: %s\n", gltfPath.c_str());
            return false;
        }

        mUniformData.drawCount = u32(drawCmds.size());

        if (!UploadTextures(texturePaths))
        {
            fprintf(stderr, "vulkan: texture uploading failed\n");
            return false;
        }

        mVertexBuffer = RHI::CreateBuffer({
            .size = VEC_SIZE_BYTES(vertices),
            .debugName = "VertexBuffer",
        });
        if (!mVertexBuffer)
        {
            return false;
        }
        memcpy(RHI::GetBufferHostPtr(mVertexBuffer), vertices.data(), VEC_SIZE_BYTES(vertices));
        RHI::UnmapBuffer(mVertexBuffer);

        mIndexBuffer = RHI::CreateBuffer({
            .size = VEC_SIZE_BYTES(indices),
            .debugName = "IndexBuffer",
        });
        if (!mIndexBuffer)
        {
            return false;
        }
        memcpy(RHI::GetBufferHostPtr(mIndexBuffer), indices.data(), VEC_SIZE_BYTES(indices));
        RHI::UnmapBuffer(mIndexBuffer);

        mDrawCmdBuffer1 = RHI::CreateBuffer({
            .size = sizeof(RHI::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debugName = "DrawCmdBuffer1",
        });
        if (!mDrawCmdBuffer1)
        {
            return false;
        }

        mDrawCmdEarlyBuffer2 = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(RHI::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debugName = "DrawCmdEarlyBuffer2",
        });
        if (!mDrawCmdEarlyBuffer2)
        {
            return false;
        }

        mDrawCmdLateBuffer2 = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(RHI::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debugName = "DrawCmdLateBuffer2",
        });
        if (!mDrawCmdLateBuffer2)
        {
            return false;
        }

        mDrawCmdShadowBuffer = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(RHI::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debugName = "DrawCmdShadowBuffer",
        });
        if (!mDrawCmdShadowBuffer)
        {
            return false;
        }

        mDrawIndicesEarlyBuffer = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debugName = "DrawIndicesEarlyBuffer",
        });
        if (!mDrawIndicesEarlyBuffer)
        {
            return false;
        }

        mDrawIndicesLateBuffer = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debugName = "DrawIndicesLateBuffer",
        });
        if (!mDrawIndicesLateBuffer)
        {
            return false;
        }

        mDrawIndicesShadowBuffer = RHI::CreateBuffer({
            .type = RHI::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debugName = "DrawIndicesShadowBuffer",
        });
        if (!mDrawIndicesShadowBuffer)
        {
            return false;
        }

        mMaterialBuffer = RHI::CreateBuffer({
            .size = sizeof(Material) * MAX_DRAW_CALLS,
            .debugName = "MaterialBuffer",
        });
        if (!mMaterialBuffer)
        {
            return false;
        }

        mDrawDataBuffer = RHI::CreateBuffer({
            .size = sizeof(DrawData) * MAX_DRAW_CALLS,
            .debugName = "DrawDataBuffer",
        });
        if (!mDrawDataBuffer)
        {
            return false;
        }

        memcpy(RHI::GetBufferHostPtr(mDrawCmdBuffer1), drawCmds.data(), VEC_SIZE_BYTES(drawCmds));

        memcpy(RHI::GetBufferHostPtr(mMaterialBuffer), materials.data(), VEC_SIZE_BYTES(materials));

        memcpy(RHI::GetBufferHostPtr(mDrawDataBuffer), drawData.data(), VEC_SIZE_BYTES(drawData));
    }

    // Command buffers
    for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
    {
        mFrame[i].startCommandBuffer = RHI::CreateCommandBuffer(RHI::QUEUE_GRAPHICS, i, "Start");
        if (!mFrame[i].startCommandBuffer)
        {
            return false;
        }
        mFrame[i].shadowCommandBuffer = RHI::CreateCommandBuffer(RHI::QUEUE_GRAPHICS, i, "Shadow");
        if (!mFrame[i].shadowCommandBuffer)
        {
            return false;
        }
        mFrame[i].ssaoCommandBuffer = RHI::CreateCommandBuffer(RHI::QUEUE_COMPUTE, i, "SSAO");
        if (!mFrame[i].ssaoCommandBuffer)
        {
            return false;
        }
        mFrame[i].endCommandBuffer = RHI::CreateCommandBuffer(RHI::QUEUE_GRAPHICS, i, "End");
        if (!mFrame[i].endCommandBuffer)
        {
            return false;
        }
    }

    if (!mImguiRenderer.Init(mWindow, RHI::GetTextureFormat(RHI::GetSwapchainTexture(0))))
    {
        fprintf(stderr, "failed to initialize ImGui renderer\n");
        return false;
    }

    // Initializing resources.
    {
        RHI::CommandBufferHandle cb = RHI::CreateCommandBuffer(RHI::QUEUE_GRAPHICS);
        if (!cb)
        {
            return false;
        }
        DEFER(RHI::DestroyCommandBuffer(cb));

        if (!RHI::BeginCommandBuffer(cb))
        {
            return false;
        }

        RHI::CmdFillBuffer(cb, mMeshPrimitiveVisibleBuffer, 0, sizeof(u32) * MAX_DRAW_CALLS, 0);

        if (!RHI::EndCommandBuffer(cb))
        {
            return false;
        }

        if (!RHI::QueueSubmit(RHI::QUEUE_GRAPHICS, {{.cb = cb}}))
        {
            return false;
        }
        (void)RHI::QueueWaitIdle(RHI::QUEUE_GRAPHICS);
    }

    mSwapchainNeedsRecreating = true;
    mTaaJitterMaxIdx = 8;
    mUniformData.taaBlendWeight = 0.1f;
    mUniformData.ambientIntensity = 0.04f;
    mUniformData.sunIntensity = 1.0f;
    mUniformData.gradErrorMax = 0.01f;
    mUniformData.enableSSAO = true;
    mUniformData.enableFog = true;

    mUniformData.shadow.enablePcf = 1;
    mUniformData.shadow.pcfKernelScale = 3.0f;
    mUniformData.shadow.pcfKernelCascadeScales[0] = 1.0f;
    mUniformData.shadow.pcfKernelCascadeScales[1] = 0.40f;
    mUniformData.shadow.pcfKernelCascadeScales[2] = 0.0f;
    mUniformData.shadow.pcfKernelCascadeScales[3] = 0.0f;
    mUniformData.shadow.normalOffset = 1.0f;
    mUniformData.shadow.constantOffset = 0.00003f;

    // TODO: try SDSM? In theory, due to 2-pass occlusion culling we have a depth pyramid
    // of last visible objects (just add a max sampler too), not sure if it's ok to use it,
    // but making another depth pyramid sounds stupid (unless it can be useful for other stuff?).
    // TODO: or, if I use mesh shaders, virtual shadow mapping can be very efficient,
    // and it looks great because of high resolution.
    mShadowCascadeRadii[0] = 3.0f;
    mShadowCascadeRadii[1] = 6.0f;
    mShadowCascadeRadii[2] = 10.0f;
    mShadowCascadeRadii[3] = 20.0f;

    return true;
}

void Renderer::Cleanup()
{
    (void)RHI::DeviceWaitIdle();

    mImguiRenderer.Cleanup();

    CleanupPipelines();
    CleanupColorResources();
    CleanupDepthResources();

    for (RHI::TextureHandle& tex : mTextures)
    {
        RHI::DestroyTexture(tex);
    }

    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        RHI::DestroyTextureDescriptor(mShadowTextureDescriptorCascade[i]);
    }

    RHI::DestroyTexture(mShadowTexture);
    RHI::DestroyTexture(mShadowPcfJitterTexture);
    RHI::DestroyBuffer(mDebugDrawCmdBuffer);
    RHI::DestroyBuffer(mDebugDrawRectBuffer);
    RHI::DestroyBuffer(mDebugDrawCountBuffer);
    RHI::DestroyBuffer(mMeshPrimitiveVisibleBuffer);
    RHI::DestroyBuffer(mDrawCountBuffer);
    RHI::DestroyBuffer(mMaterialBuffer);
    RHI::DestroyBuffer(mDrawDataBuffer);
    RHI::DestroyBuffer(mVertexBuffer);
    RHI::DestroyBuffer(mIndexBuffer);
    RHI::DestroyBuffer(mDrawIndicesShadowBuffer);
    RHI::DestroyBuffer(mDrawIndicesEarlyBuffer);
    RHI::DestroyBuffer(mDrawIndicesLateBuffer);
    RHI::DestroyBuffer(mDrawCmdShadowBuffer);
    RHI::DestroyBuffer(mDrawCmdEarlyBuffer2);
    RHI::DestroyBuffer(mDrawCmdLateBuffer2);
    RHI::DestroyBuffer(mDrawCmdBuffer1);
    for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
    {
        RHI::DestroyBuffer(mFrame[i].uniformBuffer);
    }
    RHI::DestroySemaphore(mFrameSemaphore);
    for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
    {
        RHI::DestroySemaphore(mFrame[i].startSemaphore.semaphore);
        RHI::DestroySemaphore(mFrame[i].shadowSemaphore.semaphore);
        RHI::DestroySemaphore(mFrame[i].ssaoSemaphore.semaphore);
    }
    RHI::DestroySampler(mShadowPcfJitterSampler);
    RHI::DestroySampler(mShadowSampler);
    RHI::DestroySampler(mMinSampler);
    RHI::DestroySampler(mNearestSampler);
    RHI::DestroySampler(mLinearSampler);
    RHI::DestroySampler(mTextureSampler);
    CleanupSwapchain();
    RHI::Destroy();
}

bool Renderer::StartNewFrame()
{
    DEBUG_ASSERT(!mNewFrameStarted);

    if (mRenderingPaused)
    {
        return true;
    }

    if (mUniformData.frameCount >= RHI::FRAMES_IN_FLIGHT)
    {
        RHI::WaitSemaphore(mFrameSemaphore, mUniformData.frameCount - RHI::FRAMES_IN_FLIGHT + 1);
    }

    if (!RHI::BeginNewFrame(mFrameIdx))
    {
        return false;
    }

    mImguiRenderer.StartNewFrame();

    mNewFrameStarted = true;

    return true;
}

bool Renderer::Render(f32 deltaTime)
{
    DEBUG_ASSERT(deltaTime > 0.0f);

    if (mRenderingPaused)
    {
        SDL_Delay(100);
        return true;
    }

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(mWindow, &width, &height))
    {
        SDL_PRINT_ERROR("SDL_GetWindowSizeInPixels");
        return false;
    }
    if ((mWindowSize.x != u32(width)) || (mWindowSize.y != u32(height)))
    {
        mWindowSize = {u32(width), u32(height)};
        mSwapchainNeedsRecreating = true;
    }

    if (mSwapchainNeedsRecreating)
    {
        mSwapchainNeedsRecreating = false;

        if (!CreateSwapchain(mWindowSize))
        {
            return false;
        }
    }

    Frame& frame = mFrame[mFrameIdx];

    // I can't be arsed to handle the edge cases and it's only used for debugging.
    if (mRenderModeChanged)
    {
        (void)RHI::DeviceWaitIdle();

        RHI::CommandBufferHandle cb = RHI::CreateCommandBuffer(RHI::QUEUE_GRAPHICS);
        if (!cb)
        {
            return false;
        }
        DEFER(RHI::DestroyCommandBuffer(cb));

        if (!RHI::BeginCommandBuffer(cb))
        {
            return false;
        }

        RHI::CmdBarrier(
            cb,
            RHI::STAGE_ALL_COMMANDS_BIT,
            RHI::ACCESS_MEMORY_WRITE_BIT,
            RHI::STAGE_ALL_COMMANDS_BIT,
            RHI::ACCESS_MEMORY_READ_BIT | RHI::ACCESS_MEMORY_WRITE_BIT
        );

        if (!RHI::EndCommandBuffer(cb))
        {
            return false;
        }

        if (!RHI::QueueSubmit(RHI::QUEUE_GRAPHICS, {{.cb = cb}}))
        {
            return false;
        }

        (void)RHI::DeviceWaitIdle();
    }

    RHI::TextureHandle swapchainTextureHandle{};
    RHI::SwapchainResult swapchainResult = RHI::AcquireNextSwapchainTexture(swapchainTextureHandle);

    if (swapchainResult == RHI::SWAPCHAIN_OUT_OF_DATE)
    {
        mSwapchainNeedsRecreating = true;
        return true;
    }
    else if (
        (swapchainResult != RHI::SWAPCHAIN_SUCCESS)
        && (swapchainResult != RHI::SWAPCHAIN_SUBOPTIMAL)
    )
    {
        fprintf(stderr, "RHI::AcquireNextSwapchainTexture failed\n");
        return false;
    }

    mUniformData.deltaTime = deltaTime;

    if ((mUniformData.taaEnable == 1) && (mUniformData.renderMode != RENDER_MODE_GRAD_ERROR))
    {
        const f32 haltonX = 2.0f * HaltonSequence(mTaaJitterIdx + 1, 2) - 1.0f;
        const f32 haltonY = 2.0f * HaltonSequence(mTaaJitterIdx + 1, 3) - 1.0f;
        mUniformData.prevTaaJitter = mUniformData.taaJitter;
        mUniformData.taaJitter = {
            haltonX / f32(mUniformData.renderWidth),
            haltonY / f32(mUniformData.renderHeight),
        };

        mTaaJitterIdx = (mTaaJitterIdx + 1) % mTaaJitterMaxIdx;
        // To derive, construct a translation jitter matrix and multiply with viewToClip.
        mUniformData.viewToClip(0, 2) = -mUniformData.taaJitter.X();
        mUniformData.viewToClip(1, 2) = -mUniformData.taaJitter.Y();
    }
    else
    {
        mUniformData.viewToClip(0, 2) = 0.0f;
        mUniformData.viewToClip(1, 2) = 0.0f;
    }

    mUniformData.prevWorldToClip = mUniformData.worldToClip;
    mUniformData.worldToClip = mUniformData.viewToClip * mUniformData.worldToView;
    mUniformData.clipToWorld = Inverse(mUniformData.worldToClip);

    // https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/
    // Also, niagara:
    // https://github.com/zeux/niagara
    if (!mCullCameraFrozen)
    {
        const Mat4 viewToClipT = Transpose(mUniformData.viewToClip);

        // -w <= x; x + w >= 0
        const Vec4 frustumPlaneX = NormalizePlane(viewToClipT.col[0] + viewToClipT.col[3]);
        // -w <= y; y + w >= 0
        const Vec4 frustumPlaneY = NormalizePlane(viewToClipT.col[1] + viewToClipT.col[3]);

        mUniformData.cullFrustumPlaneXX = frustumPlaneX.X();
        mUniformData.cullFrustumPlaneXZ = frustumPlaneX.Z();
        mUniformData.cullFrustumPlaneYY = frustumPlaneY.Y();
        mUniformData.cullFrustumPlaneYZ = frustumPlaneY.Z();

        mUniformData.cullWorldToView = mUniformData.worldToView;
    }

    UpdateShadowCascades();

    memcpy(RHI::GetBufferHostPtr(frame.uniformBuffer), &mUniformData, sizeof(mUniformData));

    switch (mUniformData.renderMode)
    {
    case RENDER_MODE_GRAD_ERROR:
        if (!RecordAndSubmitDebugGradError(swapchainTextureHandle))
        {
            return false;
        }
        break;
    default:
        if (!RecordAndSubmitVisibility(swapchainTextureHandle))
        {
            return false;
        }
        break;
    }

    swapchainResult = RHI::QueuePresent(RHI::QUEUE_GRAPHICS);
    if ((swapchainResult == RHI::SWAPCHAIN_OUT_OF_DATE)
        || (swapchainResult == RHI::SWAPCHAIN_SUBOPTIMAL))
    {
        mSwapchainNeedsRecreating = true;
    }
    else if (swapchainResult != RHI::SWAPCHAIN_SUCCESS)
    {
        fprintf(stderr, "RHI::QueuePresent failed\n");
        return false;
    }

    mNewFrameStarted = false;

    mPrevFrameIdx = mFrameIdx;
    mFrameIdx = (mFrameIdx + 1) % RHI::FRAMES_IN_FLIGHT;
    ++mUniformData.frameCount;
    mSwapchainRecreated = false;
    mRenderModeChanged = false;

    return true;
}

void Renderer::UpdateCamera(Vec3 position, const Mat4& worldToView)
{
    mUniformData.cameraPosition = position;
    mUniformData.worldToView = worldToView;
}

void Renderer::PauseRendering(bool paused)
{
    mRenderingPaused = paused;
}

void Renderer::ChangeRenderMode(RenderMode mode)
{
    if (mUniformData.renderMode != static_cast<u32>(mode))
    {
        mRenderModeChanged = true;
        mUniformData.renderMode = static_cast<u32>(mode);
    }
}

void Renderer::FreezeCullCamera(bool frozen)
{
    mCullCameraFrozen = frozen;
}

void Renderer::CleanupPipelines()
{
    RHI::DestroyPipeline(mShadowCullPipeline);
    RHI::DestroyPipeline(mShadowPipeline);
    RHI::DestroyPipeline(mBlurFogPipeline);
    RHI::DestroyPipeline(mFogPipeline);
    RHI::DestroyPipeline(mAmbientOcclusionUpsamplePipeline);
    RHI::DestroyPipeline(mAmbientOcclusionBlurPipeline);
    RHI::DestroyPipeline(mAmbientOcclusionPipeline);
    RHI::DestroyPipeline(mDepthViewQuarterResPipeline);
    RHI::DestroyPipeline(mDebugDrawFillCmdPipeline);
    RHI::DestroyPipeline(mDebugDrawRectPipeline);
    RHI::DestroyPipeline(mDepthReducePipeline);
    RHI::DestroyPipeline(mDebugGradErrorPipeline);
    RHI::DestroyPipeline(mTaaResolvePipeline);
    RHI::DestroyPipeline(mCullLatePipeline);
    RHI::DestroyPipeline(mCullEarlyPipeline);
    RHI::DestroyPipeline(mFullscreenPipeline);
    RHI::DestroyPipeline(mVisibilityRenderPipeline);
    RHI::DestroyPipeline(mVisibilityPipeline);
}

bool Renderer::RecompilePipelines()
{
    (void)RHI::DeviceWaitIdle();

    CleanupPipelines();

    // Graphics pipelines
    {
        Utils::FileData vertData = Utils::FileRead("VisibilityBuffer.vert.hlsl.spv");
        DEFER(free(vertData.data));
        Utils::FileData fragData = Utils::FileRead("VisibilityBuffer.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mVisibilityPipeline = RHI::CreateGraphicsPipeline({
            .bytecodes = {
                {static_cast<u8*>(vertData.data), vertData.size},
                {static_cast<u8*>(fragData.data), fragData.size},
            },
            .cull = RHI::CULL_CW,
            .depthFormat = RHI::GetTextureFormat(mDepthTexture),
            .depthMask = RHI::DEPTH_READ_BIT | RHI::DEPTH_WRITE_BIT,
            .colorTargets = {
                {
                    .format = RHI::GetTextureFormat(mVisibilityTexture),
                    .colorComponentMask = RHI::COLOR_COMPONENT_R_BIT | RHI::COLOR_COMPONENT_G_BIT
                },
            },
            .debugName = "VisibilityBufferPass",
        });
        if (!mVisibilityPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData vertData = Utils::FileRead("DebugDrawRect.vert.hlsl.spv");
        DEFER(free(vertData.data));
        Utils::FileData fragData = Utils::FileRead("DebugDrawRect.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mDebugDrawRectPipeline = RHI::CreateGraphicsPipeline({
            .bytecodes = {
                {static_cast<u8*>(vertData.data), vertData.size},
                {static_cast<u8*>(fragData.data), fragData.size},
            },
            .colorTargets = {
                {.format = RHI::GetTextureFormat(mRenderTexture)},
            },
            .debugName = "DebugDrawRectPass",
        });
        if (!mDebugDrawRectPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData vertData = Utils::FileRead("Fullscreen.vert.hlsl.spv");
        DEFER(free(vertData.data));
        Utils::FileData fragData = Utils::FileRead("Fullscreen.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mFullscreenPipeline = RHI::CreateGraphicsPipeline({
            .bytecodes = {
                {static_cast<u8*>(vertData.data), vertData.size},
                {static_cast<u8*>(fragData.data), fragData.size},
            },
            .colorTargets = {{.format = RHI::GetTextureFormat(RHI::GetSwapchainTexture(0))}},
            .debugName = "FullscreenPass",
        });
        if (!mFullscreenPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData vertData = Utils::FileRead("DebugGradError.vert.hlsl.spv");
        DEFER(free(vertData.data));
        Utils::FileData fragData = Utils::FileRead("DebugGradError.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mDebugGradErrorPipeline = RHI::CreateGraphicsPipeline({
            .bytecodes = {
                {static_cast<u8*>(vertData.data), vertData.size},
                {static_cast<u8*>(fragData.data), fragData.size},
            },
            .cull = RHI::CULL_CW,
            .depthFormat = RHI::GetTextureFormat(mDepthTexture),
            .depthMask = RHI::DEPTH_READ_BIT | RHI::DEPTH_WRITE_BIT,
            .colorTargets = {{.format = RHI::GetTextureFormat(RHI::GetSwapchainTexture(0))}},
            .debugName = "DebugGradErrorPass",
        });
        if (!mDebugGradErrorPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData vertData = Utils::FileRead("Shadow.vert.hlsl.spv");
        DEFER(free(vertData.data));
        Utils::FileData fragData = Utils::FileRead("Shadow.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mShadowPipeline = RHI::CreateGraphicsPipeline({
            .bytecodes = {
                {static_cast<u8*>(vertData.data), vertData.size},
                {static_cast<u8*>(fragData.data), fragData.size},
            },
            .cull = RHI::CULL_CW,
            .depthFormat = RHI::GetTextureFormat(mShadowTexture),
            .depthMask = RHI::DEPTH_READ_BIT | RHI::DEPTH_WRITE_BIT,
            .depthClampEnable = VK_TRUE,
            .debugName = "ShadowPass",
        });
        if (!mShadowPipeline)
        {
            return false;
        }
    }

    // Compute pipelines.
    {
        Utils::FileData compData = Utils::FileRead("CullEarly.comp.hlsl.spv");
        DEFER(free(compData.data));

        mCullEarlyPipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "CullEarlyPass",
        });
        if (!mCullEarlyPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("CullLate.comp.hlsl.spv");
        DEFER(free(compData.data));

        mCullLatePipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "CullLatePass",
        });
        if (!mCullLatePipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("ShadowCull.comp.hlsl.spv");
        DEFER(free(compData.data));

        mShadowCullPipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "ShadowCullPass",
        });
        if (!mCullLatePipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("VisibilityRender.comp.hlsl.spv");
        DEFER(free(compData.data));

        mVisibilityRenderPipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .usesBindlessTextures = true,
            .debugName = "VisibilityRenderPass",
        });
        if (!mVisibilityRenderPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("TaaResolve.comp.hlsl.spv");
        DEFER(free(compData.data));

        mTaaResolvePipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "TaaResolvePass",
        });
        if (!mTaaResolvePipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("DepthReduce.comp.hlsl.spv");
        DEFER(free(compData.data));

        mDepthReducePipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "DepthReducePass",
        });
        if (!mDepthReducePipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("DepthViewQuarterRes.comp.hlsl.spv");
        DEFER(free(compData.data));

        mDepthViewQuarterResPipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "DepthViewQuarterResPass",
        });
        if (!mDepthViewQuarterResPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("SSAO.comp.hlsl.spv");
        DEFER(free(compData.data));

        mAmbientOcclusionPipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "AmbientOcclusionPass",
        });
        if (!mAmbientOcclusionPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("BlurSSAO.comp.hlsl.spv");
        DEFER(free(compData.data));

        mAmbientOcclusionBlurPipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "AmbientOcclusionBlurPass",
        });
        if (!mAmbientOcclusionBlurPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("UpsampleSSAO.comp.hlsl.spv");
        DEFER(free(compData.data));

        mAmbientOcclusionUpsamplePipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "AmbientOcclusionUpsamplePass",
        });
        if (!mAmbientOcclusionUpsamplePipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("Fog.comp.hlsl.spv");
        DEFER(free(compData.data));

        mFogPipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "FogPass",
        });
        if (!mFogPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("BlurFog.comp.hlsl.spv");
        DEFER(free(compData.data));

        mBlurFogPipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "BlurFogPass",
        });
        if (!mBlurFogPipeline)
        {
            return false;
        }
    }

    {
        Utils::FileData compData = Utils::FileRead("DebugDrawFillCmd.comp.hlsl.spv");
        DEFER(free(compData.data));

        mDebugDrawFillCmdPipeline = RHI::CreateComputePipeline({
            .bytecode = {static_cast<u8*>(compData.data), compData.size},
            .debugName = "DebugDrawFillCmdPass",
        });
        if (!mDebugDrawFillCmdPipeline)
        {
            return false;
        }
    }

    return true;
}

void Renderer::SetSunDirection(f32 yaw, f32 pitch)
{
    DEBUG_ASSERT(yaw >= 0.0f);
    DEBUG_ASSERT(pitch >= 0.0f);

    mUniformData.sunDirectionWorld.X() = sinf(yaw) * cosf(pitch);
    mUniformData.sunDirectionWorld.Y() = -sinf(pitch);
    mUniformData.sunDirectionWorld.Z() = cosf(yaw) * cosf(pitch);
}

bool Renderer::UploadTextures(const std::vector<std::string>& texturePaths)
{
    DEBUG_ASSERT(!texturePaths.empty());

    mTextures.resize(texturePaths.size());

    for (size_t i = 0; i < texturePaths.size(); ++i)
    {
        ktxTexture2* ktxTex{};
        ktx_error_code_e ktxResult = ktxTexture2_CreateFromNamedFile(
            texturePaths[i].c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &ktxTex
        );
        if (ktxResult != KTX_SUCCESS)
        {
            fprintf(
                stderr,
                "ktx2 %s loading error: %s\n",
                texturePaths[i].c_str(),
                ktxErrorString(ktxResult)
            );
            return false;
        }
        DEFER(ktxTexture2_Destroy(ktxTex));

        const RHI::Format format = FormatToRHI(ktxTexture2_GetVkFormat(ktxTex));
        const U32Vec3 extent{ktxTex->baseWidth, ktxTex->baseHeight, ktxTex->baseDepth};
        const u32 mipLevels = ktxTex->numLevels;
        const ktx_size_t size = ktxTexture_GetDataSize(ktxTexture(ktxTex));
        const ktx_uint8_t* ktxData = ktxTexture_GetData(ktxTexture(ktxTex));

        mTextures[i] = RHI::CreateTexture({
            .format = format,
            .dimensions = {ktxTex->baseWidth, ktxTex->baseHeight, 1},
            .mipCount = mipLevels,
            .usage = RHI::TEXTURE_USAGE_SAMPLED_BIT | RHI::TEXTURE_USAGE_TRANSFER_DST_BIT,
            .debugName = texturePaths[i].c_str(),
        });
        if (!mTextures[i])
        {
            return false;
        }

        RHI::BufferHandle stagingBuffer = RHI::CreateBuffer({
            .size = size,
            .debugName = "StagingBuffer",
        });
        if (!stagingBuffer)
        {
            return false;
        }
        DEFER(RHI::DestroyBuffer(stagingBuffer));

        std::vector<RHI::BufferTextureCopy> copyRegions(mipLevels);

        for (u32 mipLevel = 0; mipLevel < mipLevels; ++mipLevel)
        {
            ktx_size_t offset = 0;
            ktxResult = ktxTexture2_GetImageOffset(ktxTex, mipLevel, 0, 0, &offset);
            if (ktxResult != KTX_SUCCESS)
            {
                fprintf(
                    stderr,
                    "ktxTexture_GetImageOffset failed: %s\n",
                    ktxErrorString(ktxResult)
                );
                return false;
            }

            copyRegions[mipLevel] = {
                .bufferOffset = offset,
                .textureSubresource = {.mipLevel = mipLevel},
                .textureDimensions = {extent.x >> mipLevel, extent.y >> mipLevel, 1},
            };
        }
        memcpy(RHI::GetBufferHostPtr(stagingBuffer), ktxData, size);
        RHI::UnmapBuffer(stagingBuffer);

        RHI::CommandBufferHandle cb = RHI::CreateCommandBuffer(RHI::QUEUE_GRAPHICS);
        if (!cb)
        {
            return false;
        }
        DEFER(RHI::DestroyCommandBuffer(cb));

        if (!RHI::BeginCommandBuffer(cb))
        {
            return false;
        }

        RHI::CmdTextureBarrier(
            cb,
            {{
                .handle = mTextures[i],
                .oldLayout = RHI::TEXTURE_LAYOUT_UNDEFINED,
                .newLayout = RHI::TEXTURE_LAYOUT_GENERAL,
                .dstStageMask = RHI::STAGE_TRANSFER_BIT,
                .dstAccessMask = RHI::ACCESS_TRANSFER_WRITE_BIT,
                .mipCount = mipLevels,
            }}
        );

        RHI::CmdCopyBufferToTexture(
            cb,
            stagingBuffer,
            mTextures[i],
            {copyRegions.data(), int(copyRegions.size())}
        );

        if (!RHI::EndCommandBuffer(cb))
        {
            return false;
        }

        if (!RHI::QueueSubmit(RHI::QUEUE_GRAPHICS, {{.cb = cb}}))
        {
            return false;
        }
        (void)RHI::QueueWaitIdle(RHI::QUEUE_GRAPHICS);
    }

    for (size_t i = 0; i < mTextures.size(); ++i)
    {
        RHI::UpdateTextureDescriptorSet(mTextures[i], u32(i));
    }

    return true;
}

void Renderer::UpdateShadowCascades()
{
    const Mat4 viewToWorld = Inverse(mUniformData.worldToView);
    const Mat4 worldToLight = LookAt(Vec3{0.0f}, mUniformData.sunDirectionWorld, WORLD_Y);

    // Calculate a combined view and orthographic projection matrix for each cascade.
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        // By using radius instead of min/max points of the frustum, we fix the projection size,
        // since a sphere can enclose any possible frustum orientation.
        // This helps with shadow map stability.
        const f32 sphereRadius = mShadowCascadeRadii[i];
        const f32 sphereDiameter = sphereRadius * 2.0f;

        const Mat4 worldToLightScaled
            = Scale(worldToLight, RENDERER_SHADOW_MAP_DIMENSIONS / sphereDiameter);
        const Mat4 lightToWorldScaled = Inverse(worldToLightScaled);

        // TODO: push as far as it can be pushed without gaps.
        Vec4 sphereCenter = {0.0f, 0.0f, -sphereRadius, 1.0f};
        sphereCenter = viewToWorld * sphereCenter;
        // Texel snapping in light space to further stabilize shadow maps.
        sphereCenter = worldToLightScaled * sphereCenter;
        sphereCenter.X() = floorf(sphereCenter.X());
        sphereCenter.Y() = floorf(sphereCenter.Y());
        sphereCenter = lightToWorldScaled * sphereCenter;

        const Mat4 lightViewMatrix = LookAt(
            sphereCenter.XYZ() - mUniformData.sunDirectionWorld * sphereRadius,
            sphereCenter.XYZ(),
            WORLD_Y
        );

        // (1, 1) is negative to flip Y.
        // clang-format off
        const Mat4 lightProjectionMatrix = {
            1.0f / sphereRadius, 0.0f, 0.0f, 0.0f,
            0.0f, -1.0f / sphereRadius, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f / sphereDiameter, 0.0f,
            0.0f, 0.0f, 1.0f, 1.0f
        };
        // clang-format on

        mUniformData.shadow.texelSizes[i] = sphereDiameter / RENDERER_SHADOW_MAP_DIMENSIONS;
        mUniformData.shadow.worldToClip[i] = lightProjectionMatrix * lightViewMatrix;
    }
}

void Renderer::VisibilityBufferPass(RHI::CommandBufferHandle cb, bool cullLate)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mVisibilityPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            cullLate ? mDrawIndicesLateBuffer : mDrawIndicesEarlyBuffer,
            mDrawDataBuffer,
            mVertexBuffer,
        }
    );

    const PushConstantsVisibilityBuffer pushConstants = {
        .cullLate = cullLate,
    };
    RHI::CmdPushConstants(cb, pipeline, &pushConstants);

    const U32Vec3 renderDim = RHI::GetTextureDimensions(mRenderTexture);

    RHI::CmdSetViewport({
        .cb = cb,
        .y = f32(renderDim.y),
        .width = f32(renderDim.x),
        .height = -f32(renderDim.y),
    });

    RHI::CmdSetScissor({.cb = cb, .extent = renderDim.XY()});

    RHI::CmdBeginRendering({
        .cb = cb,
        .extent = RHI::GetTextureDimensions(mRenderTexture).XY(),
        .colorTargets = {{
            .attachment = mVisibilityTexture,
            .loadOp = cullLate ? RHI::ATTACHMENT_LOAD_OP_LOAD : RHI::ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = RHI::ATTACHMENT_STORE_OP_STORE,
        }},
        .depthTarget = {
            .attachment = mDepthTexture,
            .loadOp = cullLate ? RHI::ATTACHMENT_LOAD_OP_LOAD : RHI::ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = RHI::ATTACHMENT_STORE_OP_STORE,
        },
    });

    RHI::CmdBindIndexBuffer(cb, mIndexBuffer, 0, RHI::INDEX_TYPE_U32);

    RHI::CmdDrawIndexedIndirectCount(
        cb,
        cullLate ? mDrawCmdLateBuffer2 : mDrawCmdEarlyBuffer2,
        0,
        mDrawCountBuffer,
        0,
        mUniformData.drawCount
    );

    RHI::CmdEndRendering(cb);
}

void Renderer::CullPass(RHI::CommandBufferHandle cb, bool late)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = late ? mCullLatePipeline : mCullEarlyPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mDrawDataBuffer,
            mDrawCountBuffer,
            mDrawCmdBuffer1,
            late ? mDrawCmdLateBuffer2 : mDrawCmdEarlyBuffer2,
            late ? mDrawIndicesLateBuffer : mDrawIndicesEarlyBuffer,
            mMeshPrimitiveVisibleBuffer,
            mMinSampler,
            // TODO: this is so fucking hacky, early pass doesn't use depth pyramid,
            // early/late pipelines are created with different specialization constants,
            // but this binding exists in both and must be filled correctly, for now
            // just using a dummy texture for the early pass.
            late ? mDepthPyramidTexture : mTextures[0],
            mDebugDrawCountBuffer,
            mDebugDrawRectBuffer,
        }
    );

    RHI::CmdDispatch(
        cb,
        {CalcDispatchSize(mUniformData.drawCount, RHI::GetPipelineLocalSize(pipeline).x), 1, 1}
    );
}

void Renderer::DepthReducePass(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mDepthReducePipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    PushConstantsDepthReduce pushConstants{};

    // In, out.
    RHI::DescriptorInfo descInfos[] = {
        mDepthTexture,
        mDepthPyramidMipTextureDescriptors[0],
        mMinSampler,
    };

    const U32Vec2 depthPyramidSize = RHI::GetTextureDimensions(mDepthPyramidTexture).XY();

    for (size_t i = 0; i < mDepthPyramidMipTextureDescriptors.size(); ++i)
    {
        descInfos[1].resource.textureDescriptor = mDepthPyramidMipTextureDescriptors[i];

        const u32 width = Max(1U, depthPyramidSize.x >> i);
        const u32 height = Max(1U, depthPyramidSize.y >> i);

        pushConstants.outWidth = width;
        pushConstants.outHeight = height;
        pushConstants.mipLevel = u32(i);

        RHI::CmdPushConstants(cb, pipeline, &pushConstants);

        RHI::CmdPushDescriptors(cb, pipeline, descInfos);

        RHI::CmdDispatch(
            cb,
            CalcDispatchSize({width, height}, RHI::GetPipelineLocalSize(pipeline))
        );

        RHI::CmdBarrier(
            cb,
            RHI::STAGE_COMPUTE_SHADER_BIT,
            RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
            RHI::STAGE_COMPUTE_SHADER_BIT,
            RHI::ACCESS_SHADER_SAMPLED_READ_BIT | RHI::ACCESS_SHADER_STORAGE_WRITE_BIT
        );

        descInfos[0].type = RHI::DescriptorInfo::TYPE_TEXTURE_DESCRIPTOR;
        descInfos[0].resource.textureDescriptor = mDepthPyramidMipTextureDescriptors[i];
    }
}

void Renderer::DepthViewQuarterResPass(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mDepthViewQuarterResPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mDepthTexture,
            mDepthViewQuarterResTexture,
        }
    );

    CmdDispatchOverTextureSize(cb, pipeline, mDepthViewQuarterResTexture);
}

void Renderer::AmbientOcclusionPass(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mAmbientOcclusionPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mDrawIndicesEarlyBuffer,
            mDrawIndicesLateBuffer,
            mDrawCmdEarlyBuffer2,
            mDrawCmdLateBuffer2,
            mDrawDataBuffer,
            mIndexBuffer,
            mVertexBuffer,
            mNearestSampler,
            mDepthViewQuarterResTexture,
            mVisibilityTexture,
            mAmbientOcclusionTexture,
        }
    );

    CmdDispatchOverTextureSize(cb, pipeline, mAmbientOcclusionTexture);
}

void Renderer::ShadowCullPass(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mShadowCullPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    const PushConstantsShadow pushConstants = {
        .shadowCascadeIdx = 0,
        .renderPassFlags = RENDER_PASS_OPAQUE_BIT,
    };
    RHI::CmdPushConstants(cb, pipeline, &pushConstants);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mDrawDataBuffer,
            mDrawCountBuffer,
            mDrawCmdBuffer1,
            mDrawCmdShadowBuffer,
            mDrawIndicesShadowBuffer,
        }
    );

    RHI::CmdDispatch(
        cb,
        {CalcDispatchSize(mUniformData.drawCount, RENDERER_CULL_WORKGROUP_SIZE), 1, 1}
    );
}

void Renderer::ShadowPass(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mShadowPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mDrawIndicesShadowBuffer,
            mDrawDataBuffer,
            mVertexBuffer,
        }
    );

    RHI::CmdSetViewport({
        .cb = cb,
        .width = f32(RENDERER_SHADOW_MAP_DIMENSIONS),
        .height = f32(RENDERER_SHADOW_MAP_DIMENSIONS),
    });

    RHI::CmdSetScissor({
        .cb = cb,
        .extent = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS},
    });

    RHI::CmdBindIndexBuffer(cb, mIndexBuffer, 0, RHI::INDEX_TYPE_U32);

    // TODO: check out VK_KHR_multiview.
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        const PushConstantsShadow pushConstants = {
            .shadowCascadeIdx = i,
            .renderPassFlags = RENDER_PASS_OPAQUE_BIT,
        };
        RHI::CmdPushConstants(cb, mShadowCullPipeline, &pushConstants);

        RHI::CmdBeginRendering({
            .cb = cb,
            .extent = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS},
            .depthTarget = {
                .attachment = mShadowTextureDescriptorCascade[i],
                .loadOp = RHI::ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = RHI::ATTACHMENT_STORE_OP_STORE,
            },
        });

        RHI::CmdDrawIndexedIndirectCount(
            cb,
            mDrawCmdShadowBuffer,
            0,
            mDrawCountBuffer,
            0,
            mUniformData.drawCount
        );

        RHI::CmdEndRendering(cb);
    }
}

void Renderer::FogPass(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mFogPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mNearestSampler,
            mDepthTexture,
            mShadowTexture,
            mFogTexture,
        }
    );

    CmdDispatchOverTextureSize(cb, pipeline, mFogTexture);
}

void Renderer::BlurFogPass(RHI::CommandBufferHandle cb, bool horizontal)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mBlurFogPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            horizontal ? mFogTexture : mFogBlurredHorizontalTexture,
            horizontal ? mFogBlurredHorizontalTexture : mFogBlurredVerticalTexture,
        }
    );

    const PushConstantsFogBlur pushConstants = {.horizontal = horizontal};
    RHI::CmdPushConstants(cb, pipeline, &pushConstants);

    CmdDispatchOverTextureSize(cb, pipeline, mFogBlurredHorizontalTexture);
}

void Renderer::AmbientOcclusionBlurPass(RHI::CommandBufferHandle cb, bool horizontal)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mAmbientOcclusionBlurPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mDepthViewQuarterResTexture,
            horizontal ? mAmbientOcclusionTexture : mAmbientOcclusionBlurredHorizontalTexture,
            horizontal ? mAmbientOcclusionBlurredHorizontalTexture
                       : mAmbientOcclusionBlurredVerticalTexture,
        }
    );

    const PushConstantsSsaoBlur pushConstants
        = horizontal ? PushConstantsSsaoBlur{1, 0} : PushConstantsSsaoBlur{0, 1};
    RHI::CmdPushConstants(cb, pipeline, &pushConstants);

    CmdDispatchOverTextureSize(cb, pipeline, mAmbientOcclusionBlurredHorizontalTexture);
}

void Renderer::AmbientOcclusionUpsamplePass(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mAmbientOcclusionUpsamplePipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mNearestSampler,
            mDepthTexture,
            mDepthViewQuarterResTexture,
            mAmbientOcclusionBlurredVerticalTexture,
            mAmbientOcclusionUpsampledTexture,
        }
    );

    CmdDispatchOverTextureSize(cb, pipeline, mAmbientOcclusionUpsampledTexture);
}

void Renderer::RenderPass(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mVisibilityRenderPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mDrawIndicesEarlyBuffer,
            mDrawIndicesLateBuffer,
            mDrawCmdEarlyBuffer2,
            mDrawCmdLateBuffer2,
            mDrawDataBuffer,
            mIndexBuffer,
            mVertexBuffer,
            mMaterialBuffer,
            mLinearSampler,
            mTextureSampler,
            mShadowSampler,
            mShadowPcfJitterSampler,
            mShadowTexture,
            mShadowPcfJitterTexture,
            mFogBlurredVerticalTexture,
            mVisibilityTexture,
            mAmbientOcclusionUpsampledTexture,
            mVelocityTexture,
            mRenderTexture,
        }
    );

    RHI::CmdBindTextureDescriptorSet(cb, pipeline);

    CmdDispatchOverTextureSize(cb, pipeline, mRenderTexture);
}

void Renderer::TaaResolvePass(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    const RHI::PipelineHandle pipeline = mTaaResolvePipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mRenderTexture,
            mDepthTexture,
            mVelocityTexture,
            mSwapchainRecreated || mRenderModeChanged ? mFrame[mFrameIdx].resolvedRenderTexture
                                                      : mFrame[mPrevFrameIdx].resolvedRenderTexture,
            mFrame[mFrameIdx].resolvedRenderTexture,
            mLinearSampler,
        }
    );

    CmdDispatchOverTextureSize(cb, pipeline, mFrame[mFrameIdx].resolvedRenderTexture);
}

void Renderer::DebugDrawPass(RHI::CommandBufferHandle cb)
{
    DEBUG_ASSERT(cb);

    RHI::CmdBindPipeline(cb, mDebugDrawFillCmdPipeline);

    RHI::CmdPushDescriptors(
        cb,
        mDebugDrawFillCmdPipeline,
        {
            mDebugDrawCountBuffer,
            mDebugDrawCmdBuffer,
        }
    );

    RHI::CmdDispatch(cb, {1, 1, 1});

    RHI::CmdBarrier(
        cb,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_DRAW_INDIRECT_BIT,
        RHI::ACCESS_INDIRECT_COMMAND_READ_BIT
    );

    RHI::CmdBindPipeline(cb, mDebugDrawRectPipeline);

    RHI::CmdPushDescriptors(
        cb,
        mDebugDrawRectPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            mDebugDrawCountBuffer,
            mDebugDrawRectBuffer,
        }
    );

    const U32Vec2 size = RHI::GetTextureDimensions(RHI::GetSwapchainTexture(0)).XY();

    RHI::CmdSetViewport({
        .cb = cb,
        .width = f32(size.x),
        .height = f32(size.y),
    });

    RHI::CmdSetScissor({.cb = cb, .extent = size});

    RHI::CmdBeginRendering({
        .cb = cb,
        .extent = size,
        .colorTargets = {{
            .attachment = mFrame[mFrameIdx].resolvedRenderTexture,
            .loadOp = RHI::ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = RHI::ATTACHMENT_STORE_OP_STORE,
        }},
    });

    RHI::CmdDrawIndirect(cb, mDebugDrawCmdBuffer, 0, 1, 0);

    RHI::CmdEndRendering(cb);
}

void Renderer::FullscreenPass(RHI::CommandBufferHandle cb, RHI::TextureHandle swapchainTexture)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(swapchainTexture);

    const RHI::PipelineHandle pipeline = mFullscreenPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].resolvedRenderTexture,
            mLinearSampler,
        }
    );

    const U32Vec2 size = RHI::GetTextureDimensions(swapchainTexture).XY();

    RHI::CmdSetViewport({
        .cb = cb,
        .y = f32(size.y),
        .width = f32(size.x),
        .height = -f32(size.y),
    });

    RHI::CmdSetScissor({.cb = cb, .extent = size});

    RHI::CmdBeginRendering({
        .cb = cb,
        .extent = size,
        .colorTargets = {{
            .attachment = swapchainTexture,
            .loadOp = RHI::ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = RHI::ATTACHMENT_STORE_OP_STORE,
        }},
    });

    RHI::CmdDraw(cb, 3, 1, 0, 0);

    RHI::CmdEndRendering(cb);
}

void Renderer::DebugDrawGradErrorPass(
    RHI::CommandBufferHandle cb,
    bool cullLate,
    RHI::TextureHandle swapchainTexture
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(swapchainTexture);

    const RHI::PipelineHandle pipeline = mDebugGradErrorPipeline;

    RHI::CmdBindPipeline(cb, pipeline);

    RHI::CmdPushDescriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].uniformBuffer,
            cullLate ? mDrawIndicesLateBuffer : mDrawIndicesEarlyBuffer,
            cullLate ? mDrawCmdLateBuffer2 : mDrawCmdEarlyBuffer2,
            mDrawDataBuffer,
            mIndexBuffer,
            mVertexBuffer,
        }
    );

    const U32Vec2 size = RHI::GetTextureDimensions(swapchainTexture).XY();

    RHI::CmdSetViewport({
        .cb = cb,
        .y = f32(size.y),
        .width = f32(size.x),
        .height = -f32(size.y),
    });

    RHI::CmdSetScissor({.cb = cb, .extent = size});

    RHI::CmdBeginRendering({
        .cb = cb,
        .extent = size,
        .colorTargets = {{
            .attachment = swapchainTexture,
            .loadOp = cullLate ? RHI::ATTACHMENT_LOAD_OP_LOAD : RHI::ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = RHI::ATTACHMENT_STORE_OP_STORE,
        }},
        .depthTarget = {
            .attachment = mDepthTexture,
            .loadOp = cullLate ? RHI::ATTACHMENT_LOAD_OP_LOAD : RHI::ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = RHI::ATTACHMENT_STORE_OP_STORE,
        },
    });

    RHI::CmdBindIndexBuffer(cb, mIndexBuffer, 0, RHI::INDEX_TYPE_U32);

    RHI::CmdDrawIndexedIndirectCount(
        cb,
        cullLate ? mDrawCmdLateBuffer2 : mDrawCmdEarlyBuffer2,
        0,
        mDrawCountBuffer,
        0,
        mUniformData.drawCount
    );

    RHI::CmdEndRendering(cb);
}

bool Renderer::RecordAndSubmitDebugGradError(RHI::TextureHandle swapchainTexture)
{
    DEBUG_ASSERT(swapchainTexture);

    const RHI::CommandBufferHandle cb = mFrame[mFrameIdx].startCommandBuffer;

    if (!RHI::BeginCommandBuffer(cb))
    {
        return false;
    }

    RHI::CmdTextureInvalidateBarrier(
        cb,
        RHI::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | RHI::STAGE_LATE_FRAGMENT_TESTS_BIT,
        RHI::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        RHI::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | RHI::STAGE_COMPUTE_SHADER_BIT
            | RHI::STAGE_EARLY_FRAGMENT_TESTS_BIT,
        RHI::ACCESS_COLOR_ATTACHMENT_WRITE_BIT | RHI::ACCESS_SHADER_STORAGE_WRITE_BIT
            | RHI::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        {
            mVisibilityTexture,
            mDepthPyramidTexture,
            swapchainTexture,
            mDepthTexture,
        }
    );

    RHI::CmdBarrier(
        cb,
        RHI::STAGE_DRAW_INDIRECT_BIT,
        RHI::ACCESS_NONE,
        RHI::STAGE_TRANSFER_BIT,
        RHI::ACCESS_NONE
    );

    RHI::CmdFillBuffer(cb, mDrawCountBuffer, 0, sizeof(u32), 0);

    RHI::CmdBarrier(
        cb,
        RHI::STAGE_TRANSFER_BIT | RHI::STAGE_VERTEX_SHADER_BIT | RHI::STAGE_FRAGMENT_SHADER_BIT,
        RHI::ACCESS_TRANSFER_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_READ_BIT | RHI::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    CullPass(cb, false);

    RHI::CmdBarrier(
        cb,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_DRAW_INDIRECT_BIT | RHI::STAGE_VERTEX_SHADER_BIT
            | RHI::STAGE_FRAGMENT_SHADER_BIT,
        RHI::ACCESS_INDIRECT_COMMAND_READ_BIT | RHI::ACCESS_SHADER_STORAGE_READ_BIT
    );

    DebugDrawGradErrorPass(cb, false, swapchainTexture);

    RHI::CmdBarrier(
        cb,
        RHI::STAGE_LATE_FRAGMENT_TESTS_BIT,
        RHI::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    if (!mCullCameraFrozen)
    {
        DepthReducePass(cb);
    }

    RHI::CmdBarrier(
        cb,
        RHI::STAGE_VERTEX_SHADER_BIT | RHI::STAGE_DRAW_INDIRECT_BIT,
        RHI::ACCESS_NONE,
        RHI::STAGE_COMPUTE_SHADER_BIT | RHI::STAGE_TRANSFER_BIT,
        RHI::ACCESS_NONE
    );

    RHI::CmdFillBuffer(cb, mDrawCountBuffer, 0, sizeof(u32), 0);

    RHI::CmdBarrier(
        cb,
        RHI::STAGE_TRANSFER_BIT | RHI::STAGE_VERTEX_SHADER_BIT | RHI::STAGE_FRAGMENT_SHADER_BIT
            | RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_TRANSFER_WRITE_BIT | RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_READ_BIT | RHI::ACCESS_SHADER_SAMPLED_READ_BIT
            | RHI::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    CullPass(cb, true);

    RHI::CmdBarrier(
        cb,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_DRAW_INDIRECT_BIT | RHI::STAGE_VERTEX_SHADER_BIT | RHI::STAGE_FRAGMENT_SHADER_BIT
            | RHI::STAGE_COMPUTE_SHADER_BIT | RHI::STAGE_EARLY_FRAGMENT_TESTS_BIT,
        RHI::ACCESS_INDIRECT_COMMAND_READ_BIT | RHI::ACCESS_SHADER_STORAGE_READ_BIT
            | RHI::ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
    );

    DebugDrawGradErrorPass(cb, true, swapchainTexture);

    if (!mImguiRenderer.UpdateVertexIndexBuffers(static_cast<u32>(mFrameIdx)))
    {
        return false;
    }

    // TODO: separate pass.
    if (mEnableUI)
    {
        const U32Vec2 size = RHI::GetTextureDimensions(swapchainTexture).XY();

        RHI::CmdBeginRendering({
            .cb = cb,
            .extent = size,
            .colorTargets = {{
                .attachment = swapchainTexture,
                .loadOp = RHI::ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = RHI::ATTACHMENT_STORE_OP_STORE,
            }},
        });

        if (!mImguiRenderer.Render(cb, u32(mFrameIdx)))
        {
            return false;
        }

        RHI::CmdEndRendering(cb);
    }

    RHI::CmdTextureBarrier(
        cb,
        {{
            swapchainTexture,
            RHI::TEXTURE_LAYOUT_GENERAL,
            RHI::TEXTURE_LAYOUT_PRESENT_SRC,
            RHI::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            RHI::ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            RHI::STAGE_NONE,
            RHI::ACCESS_NONE,
        }}
    );

    if (!RHI::EndCommandBuffer(cb))
    {
        return false;
    }

    if (!RHI::QueueSubmit(
            RHI::QUEUE_GRAPHICS,
            {{
                .cb = cb,
                .waitForTextureAcquire = true,
                .signalReadyToPresent = true,
                .signalSemaphores = {
                    {mFrameSemaphore, mUniformData.frameCount + 1},
                },
            }}
        ))
    {
        return false;
    }

    return true;
}

bool Renderer::RecordAndSubmitVisibility(RHI::TextureHandle swapchainTexture)
{
    Frame& frame = mFrame[mFrameIdx];

    const RHI::CommandBufferHandle cbStart = frame.startCommandBuffer;
    if (!RHI::BeginCommandBuffer(cbStart))
    {
        return false;
    }

    RHI::CmdTextureInvalidateBarrier(
        cbStart,
        RHI::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        RHI::ACCESS_NONE,
        RHI::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | RHI::STAGE_EARLY_FRAGMENT_TESTS_BIT
            | RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_COLOR_ATTACHMENT_WRITE_BIT | RHI::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            | RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        {
            mVisibilityTexture,
            mDepthPyramidTexture,
            mAmbientOcclusionTexture,
            mDepthViewQuarterResTexture,
            mAmbientOcclusionBlurredHorizontalTexture,
            mAmbientOcclusionBlurredVerticalTexture,
            mAmbientOcclusionUpsampledTexture,
            mRenderTexture,
            mVelocityTexture,
            mFogTexture,
            mFogBlurredHorizontalTexture,
            mFogBlurredVerticalTexture,
            frame.resolvedRenderTexture,
            swapchainTexture,
            mDepthTexture,
            mShadowTexture,
        }
    );

    RHI::CmdFillBuffer(cbStart, mDrawCountBuffer, 0, sizeof(u32), 0);
    RHI::CmdFillBuffer(cbStart, mDebugDrawCountBuffer, 0, sizeof(u32), 0);

    RHI::CmdBarrier(
        cbStart,
        RHI::STAGE_TRANSFER_BIT,
        RHI::ACCESS_TRANSFER_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_READ_BIT | RHI::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    CullPass(cbStart, false);

    RHI::CmdBarrier(
        cbStart,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_DRAW_INDIRECT_BIT | RHI::STAGE_VERTEX_SHADER_BIT,
        RHI::ACCESS_INDIRECT_COMMAND_READ_BIT | RHI::ACCESS_SHADER_STORAGE_READ_BIT
    );

    VisibilityBufferPass(cbStart, false);

    RHI::CmdBarrier(
        cbStart,
        RHI::STAGE_LATE_FRAGMENT_TESTS_BIT,
        RHI::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    if (!mCullCameraFrozen)
    {
        DepthReducePass(cbStart);
    }

    RHI::CmdBarrier(
        cbStart,
        RHI::STAGE_VERTEX_SHADER_BIT | RHI::STAGE_DRAW_INDIRECT_BIT,
        RHI::ACCESS_NONE,
        RHI::STAGE_COMPUTE_SHADER_BIT | RHI::STAGE_TRANSFER_BIT,
        RHI::ACCESS_NONE
    );

    RHI::CmdFillBuffer(cbStart, mDrawCountBuffer, 0, sizeof(u32), 0);

    RHI::CmdBarrier(
        cbStart,
        RHI::STAGE_TRANSFER_BIT | RHI::STAGE_VERTEX_SHADER_BIT | RHI::STAGE_FRAGMENT_SHADER_BIT
            | RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_TRANSFER_WRITE_BIT | RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_READ_BIT | RHI::ACCESS_SHADER_SAMPLED_READ_BIT
            | RHI::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    CullPass(cbStart, true);

    RHI::CmdBarrier(
        cbStart,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_DRAW_INDIRECT_BIT | RHI::STAGE_VERTEX_SHADER_BIT | RHI::STAGE_COMPUTE_SHADER_BIT
            | RHI::STAGE_EARLY_FRAGMENT_TESTS_BIT,
        RHI::ACCESS_INDIRECT_COMMAND_READ_BIT | RHI::ACCESS_SHADER_STORAGE_READ_BIT
            | RHI::ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
    );

    VisibilityBufferPass(cbStart, true);

    if (!RHI::EndCommandBuffer(cbStart))
    {
        return false;
    }

    // TODO: I don't see why can't I use 2 submits per frame (1 for each queue).
    if (!RHI::QueueSubmit(
            RHI::QUEUE_GRAPHICS,
            {{
                .cb = cbStart,
                .waitForTextureAcquire = true,
                .signalSemaphores = {
                    {frame.startSemaphore.semaphore, frame.startSemaphore.Inc()},
                },
            }}
        ))
    {
        return false;
    }

    const RHI::CommandBufferHandle cbSSAO = frame.ssaoCommandBuffer;
    if (!RHI::BeginCommandBuffer(cbSSAO))
    {
        return false;
    }

    DepthViewQuarterResPass(cbSSAO);

    RHI::CmdBarrier(
        cbSSAO,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    AmbientOcclusionPass(cbSSAO);

    RHI::CmdBarrier(
        cbSSAO,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    AmbientOcclusionBlurPass(cbSSAO, true);

    RHI::CmdBarrier(
        cbSSAO,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    AmbientOcclusionBlurPass(cbSSAO, false);

    RHI::CmdBarrier(
        cbSSAO,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    AmbientOcclusionUpsamplePass(cbSSAO);

    if (!RHI::EndCommandBuffer(cbSSAO))
    {
        return false;
    }

    if (!RHI::QueueSubmit(
            RHI::QUEUE_COMPUTE,
            {{
                .cb = cbSSAO,
                .waitSemaphores = {
                    {frame.startSemaphore.semaphore, frame.startSemaphore.value},
                },
                .signalSemaphores = {
                    {frame.ssaoSemaphore.semaphore, frame.ssaoSemaphore.Inc()},
                },
            }}
        ))
    {
        return false;
    }

    const RHI::CommandBufferHandle cbShadow = frame.shadowCommandBuffer;
    if (!RHI::BeginCommandBuffer(cbShadow))
    {
        return false;
    }

    RHI::CmdFillBuffer(cbShadow, mDrawCountBuffer, 0, sizeof(u32), 0);

    RHI::CmdBarrier(
        cbShadow,
        RHI::STAGE_TRANSFER_BIT,
        RHI::ACCESS_TRANSFER_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_READ_BIT | RHI::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    ShadowCullPass(cbShadow);

    RHI::CmdBarrier(
        cbShadow,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_VERTEX_SHADER_BIT | RHI::STAGE_DRAW_INDIRECT_BIT,
        RHI::ACCESS_SHADER_STORAGE_READ_BIT | RHI::ACCESS_INDIRECT_COMMAND_READ_BIT
    );

    ShadowPass(cbShadow);

    if (!RHI::EndCommandBuffer(cbShadow))
    {
        return false;
    }

    if (!RHI::QueueSubmit(
            RHI::QUEUE_GRAPHICS,
            {{
                .cb = cbShadow,
                .waitSemaphores = {
                    {frame.startSemaphore.semaphore, frame.startSemaphore.value},
                },
                .signalSemaphores = {
                    {frame.shadowSemaphore.semaphore, frame.shadowSemaphore.Inc()},
                },
            }}
        ))
    {
        return false;
    }

    const RHI::CommandBufferHandle cbEnd = frame.endCommandBuffer;
    if (!RHI::BeginCommandBuffer(cbEnd))
    {
        return false;
    }

    FogPass(cbEnd);

    RHI::CmdBarrier(
        cbEnd,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    BlurFogPass(cbEnd, true);

    RHI::CmdBarrier(
        cbEnd,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    BlurFogPass(cbEnd, false);

    RHI::CmdBarrier(
        cbEnd,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    RenderPass(cbEnd);

    RHI::CmdBarrier(
        cbEnd,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    TaaResolvePass(cbEnd);

    RHI::CmdBarrier(
        cbEnd,
        RHI::STAGE_COMPUTE_SHADER_BIT,
        RHI::ACCESS_SHADER_STORAGE_WRITE_BIT,
        RHI::STAGE_FRAGMENT_SHADER_BIT | RHI::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT | RHI::ACCESS_COLOR_ATTACHMENT_READ_BIT
    );

    DebugDrawPass(cbEnd);

    RHI::CmdBarrier(
        cbEnd,
        RHI::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        RHI::ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        RHI::STAGE_FRAGMENT_SHADER_BIT,
        RHI::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    FullscreenPass(cbEnd, swapchainTexture);

    if (!mImguiRenderer.UpdateVertexIndexBuffers(static_cast<u32>(mFrameIdx)))
    {
        return false;
    }

    if (mEnableUI)
    {
        const U32Vec2 size = RHI::GetTextureDimensions(swapchainTexture).XY();

        RHI::CmdBeginRendering({
            .cb = cbEnd,
            .extent = size,
            .colorTargets = {{
                .attachment = swapchainTexture,
                .loadOp = RHI::ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = RHI::ATTACHMENT_STORE_OP_STORE,
            }},
        });

        if (!mImguiRenderer.Render(cbEnd, u32(mFrameIdx)))
        {
            return false;
        }

        RHI::CmdEndRendering(cbEnd);
    }

    RHI::CmdTextureBarrier(
        cbEnd,
        {{
            swapchainTexture,
            RHI::TEXTURE_LAYOUT_GENERAL,
            RHI::TEXTURE_LAYOUT_PRESENT_SRC,
            RHI::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            RHI::ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            RHI::STAGE_NONE,
            RHI::ACCESS_NONE,
        }}
    );

    if (!RHI::EndCommandBuffer(cbEnd))
    {
        return false;
    }

    if (!RHI::QueueSubmit(
            RHI::QUEUE_GRAPHICS,
            {{
                .cb = cbEnd,
                .waitSemaphores = {
                    {frame.ssaoSemaphore.semaphore, frame.ssaoSemaphore.value},
                    {frame.shadowSemaphore.semaphore, frame.shadowSemaphore.value},
                },
                .signalReadyToPresent = true,
                .signalSemaphores = {
                    {mFrameSemaphore, mUniformData.frameCount + 1},
                },
            }}
    ))
    {
        return false;
    }

    return true;
}

bool Renderer::CreateSwapchain(U32Vec2 size)
{
    (void)RHI::DeviceWaitIdle();

    CleanupSwapchain();
    CleanupColorResources();
    CleanupDepthResources();

    if (!RHI::CreateSwapchain(size))
    {
        return false;
    }

    mUniformData.swapchainWidth = size.x;
    mUniformData.swapchainHeight = size.y;
    mUniformData.viewToClip
        = Perspective(FOV_Y_RAD, f32(size.x) / f32(size.y), RENDERER_NEAR_PLANE);
    mUniformData.viewToClipInv0011 = {
        1.0f / mUniformData.viewToClip(0, 0),
        1.0f / mUniformData.viewToClip(1, 1),
    };
    mUniformData.prevWorldToClip = mUniformData.worldToClip;
    mUniformData.worldToClip = mUniformData.viewToClip * mUniformData.worldToView;
    mUniformData.clipToWorld = Inverse(mUniformData.worldToClip);

    if (!CreateColorResources())
    {
        return false;
    }

    if (!CreateDepthResources())
    {
        return false;
    }

    mSwapchainRecreated = true;

    return true;
}

void Renderer::CleanupSwapchain()
{
    RHI::DestroySwapchain();
}

bool Renderer::CreateColorResources()
{
    const U32Vec2 swapchainSize = RHI::GetTextureDimensions(RHI::GetSwapchainTexture(0)).XY();
    const U32Vec3 renderDimensions
        = {swapchainSize.x * RENDER_SCALE, swapchainSize.y * RENDER_SCALE, 1};

    mRenderTexture = RHI::CreateTexture({
        .format = RHI::FORMAT_B10G11R11_UFLOAT_PACK32,
        .dimensions = renderDimensions,
        .usage = RHI::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RHI::TEXTURE_USAGE_STORAGE_BIT
            | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "RenderTexture",
    });
    if (!mRenderTexture)
    {
        return false;
    }

    mUniformData.renderWidth = renderDimensions.x;
    mUniformData.renderHeight = renderDimensions.y;
    mUniformData.aspect = f32(mUniformData.renderWidth) / f32(mUniformData.renderHeight);
    mUniformData.renderTextureSizeInv = {1.0f / renderDimensions.x, 1.0f / renderDimensions.y};

    mVisibilityTexture = RHI::CreateTexture({
        .format = RHI::FORMAT_R32G32_UINT,
        .dimensions = renderDimensions,
        .usage = RHI::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "VisibilityTexture",
    });
    if (!mVisibilityTexture)
    {
        return false;
    }

    const U32Vec3 aoDimensions = {renderDimensions.x / 2, renderDimensions.y / 2, 1};

    mUniformData.ambientOcclusionWidth = aoDimensions.x;
    mUniformData.ambientOcclusionHeight = aoDimensions.y;
    mUniformData.ambientOcclusionTextureSizeInv
        = {1.0f / f32(aoDimensions.x), 1.0f / f32(aoDimensions.y)};

    mAmbientOcclusionTexture = RHI::CreateTexture({
        .format = RHI::FORMAT_R8_UNORM,
        .dimensions = aoDimensions,
        .usage = RHI::TEXTURE_USAGE_STORAGE_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "AmbientOcclusionTexture",
    });
    if (!mAmbientOcclusionTexture)
    {
        return false;
    }

    mAmbientOcclusionBlurredHorizontalTexture = RHI::CreateTexture({
        .format = RHI::GetTextureFormat(mAmbientOcclusionTexture),
        .dimensions = aoDimensions,
        .usage = RHI::TEXTURE_USAGE_STORAGE_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "AmbientOcclusionBlurredHorizontalTexture",
    });
    if (!mAmbientOcclusionBlurredHorizontalTexture)
    {
        return false;
    }

    mAmbientOcclusionBlurredVerticalTexture = RHI::CreateTexture({
        .format = RHI::GetTextureFormat(mAmbientOcclusionTexture),
        .dimensions = aoDimensions,
        .usage = RHI::TEXTURE_USAGE_STORAGE_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "AmbientOcclusionBlurredVerticalTexture",
    });
    if (!mAmbientOcclusionBlurredVerticalTexture)
    {
        return false;
    }

    mAmbientOcclusionUpsampledTexture = RHI::CreateTexture({
        .format = RHI::GetTextureFormat(mAmbientOcclusionTexture),
        .dimensions = renderDimensions,
        .usage = RHI::TEXTURE_USAGE_STORAGE_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "AmbientOcclusionBlurredUpsampledTexture",
    });
    if (!mAmbientOcclusionUpsampledTexture)
    {
        return false;
    }

    mFogTexture = RHI::CreateTexture({
        .format = RHI::FORMAT_R16_SFLOAT,
        .dimensions = renderDimensions,
        .usage = RHI::TEXTURE_USAGE_STORAGE_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "FogTexture",
    });
    if (!mFogTexture)
    {
        return false;
    }

    mFogBlurredHorizontalTexture = RHI::CreateTexture({
        .format = RHI::GetTextureFormat(mFogTexture),
        .dimensions = renderDimensions,
        .usage = RHI::TEXTURE_USAGE_STORAGE_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "FogBlurredHorizontalTexture",
    });
    if (!mFogBlurredHorizontalTexture)
    {
        return false;
    }

    mFogBlurredVerticalTexture = RHI::CreateTexture({
        .format = RHI::GetTextureFormat(mFogTexture),
        .dimensions = renderDimensions,
        .usage = RHI::TEXTURE_USAGE_STORAGE_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "FogBlurredVerticalTexture",
    });
    if (!mFogBlurredVerticalTexture)
    {
        return false;
    }

    for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
    {
        mFrame[i].resolvedRenderTexture = RHI::CreateTexture({
            .format = RHI::GetTextureFormat(mRenderTexture),
            .dimensions = renderDimensions,
            .usage = RHI::TEXTURE_USAGE_STORAGE_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT
                | RHI::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT,
            .debugName = "ResolvedRenderTexture",
        });
        if (!mFrame[i].resolvedRenderTexture)
        {
            return false;
        }
    }

    mVelocityTexture = RHI::CreateTexture({
        .format = RHI::FORMAT_R16G16_SFLOAT,
        .dimensions = renderDimensions,
        .usage = RHI::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RHI::TEXTURE_USAGE_STORAGE_BIT
            | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "VelocityTexture",
    });
    if (!mVelocityTexture)
    {
        return false;
    }

    return true;
}

void Renderer::CleanupColorResources()
{
    for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
    {
        RHI::DestroyTexture(mFrame[i].resolvedRenderTexture);
    }
    RHI::DestroyTexture(mFogBlurredVerticalTexture);
    RHI::DestroyTexture(mFogBlurredHorizontalTexture);
    RHI::DestroyTexture(mFogTexture);
    RHI::DestroyTexture(mAmbientOcclusionUpsampledTexture);
    RHI::DestroyTexture(mAmbientOcclusionBlurredVerticalTexture);
    RHI::DestroyTexture(mAmbientOcclusionBlurredHorizontalTexture);
    RHI::DestroyTexture(mAmbientOcclusionTexture);
    RHI::DestroyTexture(mVisibilityTexture);
    RHI::DestroyTexture(mRenderTexture);
    RHI::DestroyTexture(mVelocityTexture);
}

bool Renderer::CreateDepthResources()
{
    const U32Vec3 renderDimensions = RHI::GetTextureDimensions(mRenderTexture);

    mDepthTexture = RHI::CreateTexture({
        .format = RHI::FORMAT_D32_SFLOAT,
        .dimensions = renderDimensions,
        .usage = RHI::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "DepthTexture",
    });
    if (!mDepthTexture)
    {
        return false;
    }

    mDepthViewQuarterResTexture = RHI::CreateTexture({
        .format = RHI::FORMAT_R16_SFLOAT,
        .dimensions = {renderDimensions.x / 2, renderDimensions.y / 2, 1},
        .usage = RHI::TEXTURE_USAGE_STORAGE_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "DepthViewQuarterResTexture",
    });
    if (!mDepthViewQuarterResTexture)
    {
        return false;
    }

    const U32Vec2 depthPyramidSize
        = {PreviousPow2(renderDimensions.x), PreviousPow2(renderDimensions.y)};
    const u32 depthPyramidMipLevels = Utils::GetMipLevels(depthPyramidSize.x, depthPyramidSize.y);

    // PreviousPow2 to make reductions at most by 2x2, otherwise they are not conservative.
    mDepthPyramidTexture = RHI::CreateTexture({
        .format = RHI::FORMAT_R32_SFLOAT,
        .dimensions = {depthPyramidSize.x, depthPyramidSize.y, 1},
        .mipCount = depthPyramidMipLevels,
        .usage = RHI::TEXTURE_USAGE_STORAGE_BIT | RHI::TEXTURE_USAGE_SAMPLED_BIT,
        .debugName = "DepthPyramidTexture",
    });
    if (!mDepthPyramidTexture)
    {
        return false;
    }

    mUniformData.depthPyramidWidth = f32(depthPyramidSize.x);
    mUniformData.depthPyramidHeight = f32(depthPyramidSize.y);

    mDepthPyramidMipTextureDescriptors.resize(depthPyramidMipLevels);

    for (size_t i = 0; i < mDepthPyramidMipTextureDescriptors.size(); ++i)
    {
        mDepthPyramidMipTextureDescriptors[i] = RHI::CreateTextureDescriptor({
            .textureHandle = mDepthPyramidTexture,
            .type = RHI::TEXTURE_TYPE_2D,
            .baseMip = u32(i),
            .mipCount = 1,
            .layerCount = 1,
        });
        if (!mDepthPyramidMipTextureDescriptors[i])
        {
            return false;
        }
    }

    return true;
}

void Renderer::CleanupDepthResources()
{
    RHI::DestroyTexture(mDepthTexture);
    RHI::DestroyTexture(mDepthViewQuarterResTexture);
    RHI::DestroyTexture(mDepthPyramidTexture);
    for (RHI::TextureDescriptorHandle h : mDepthPyramidMipTextureDescriptors)
    {
        RHI::DestroyTextureDescriptor(h);
    }
}
