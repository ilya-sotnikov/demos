#include "renderer.hpp"

#include "math/mat4.hpp"
#include "math/vec2.hpp"
#include "renderer/rhi/vulkan/type_convert.hpp"
#include "utils.hpp"

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

static Vec3U32 CalcDispatchSize(Vec2U32 textureSize, Vec3U32 localSize)
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
    rhi::CommandBuffer cb,
    rhi::Pipeline pipeline,
    rhi::Texture texture
)
{
    rhi::cmd_dispatch(
        cb,
        CalcDispatchSize(
            rhi::get_texture_dimensions(texture).xy(),
            rhi::get_pipeline_local_size(pipeline)
        )
    );
}

[[maybe_unused]]
static void CmdFullBarrier(rhi::CommandBuffer cb)
{
    rhi::cmd_barrier(
        cb,
        rhi::STAGE_ALL_COMMANDS_BIT,
        rhi::ACCESS_MEMORY_WRITE_BIT,
        rhi::STAGE_ALL_COMMANDS_BIT,
        rhi::ACCESS_MEMORY_READ_BIT | rhi::ACCESS_MEMORY_WRITE_BIT
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

// Adapted from Sky.hlsli, assuming viewDirectionWorld == -sunDirectionWorld.
static Vec3 CalcSunColor(Vec3 sunDirectionWorld)
{
    const f32 I0 = 11.0f;
    const Vec3 sigmaRayleigh = Vec3{0.33f, 0.78f, 1.89f};
    const f32 sRayleigh = 0.17f;
    const f32 sMie = 0.05f;
    const f32 sSun = 0.07f;
    const f32 wMie = 0.25f;
    const f32 wSun = 0.0002f;

    const Vec3 viewDirectionWorld = -sunDirectionWorld;

    const f32 dotViewSun = Dot(viewDirectionWorld, sunDirectionWorld);
    // Tweaking a little to prevent divisions by zero.
    const f32 sunY = sunDirectionWorld.Y() + 1e-5f;
    const f32 viewY = viewDirectionWorld.Y() + 2e-5f;

    const f32 phaseRayleigh = 3.0f / (16.0f * M_PIf) * (1.0f + dotViewSun * dotViewSun);

    f32 phaseMie = wMie / (1.0f + wMie + dotViewSun);
    phaseMie = 0.3f * phaseMie * phaseMie;

    f32 phaseSun = wSun / (1.0f + wSun + dotViewSun);
    phaseSun = 4.0f * phaseSun * phaseSun;

    const Vec3 sigmaSum = sRayleigh * sigmaRayleigh + Vec3{sMie};
    const Vec3 phaseSum
        = sRayleigh * sigmaRayleigh * phaseRayleigh + Vec3{sMie * phaseMie + sSun * phaseSun};

    const f32 surfaceDarkening = viewDirectionWorld.Y() < 0.0f ? 0.3f : 1.0f;

    return I0 * surfaceDarkening * sunY / (sunY + viewY) * (phaseSum / sigmaSum)
        * (Exp(sigmaSum / sunY) - Exp(-sigmaSum / Abs(viewY)));
}

bool Renderer::Init()
{
    mScratchArena.init(16'000'000);

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

    rhi::create(mWindow);

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(mWindow, &width, &height))
    {
        SDL_PRINT_ERROR("SDL_GetWindowSizeInPixels");
        return false;
    }

    CreateSwapchain({u32(width), u32(height)});

    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i)
    {
        mFrame[i].uniformBufferManager.init(
            16'000,
            int(rhi::get_device_properties().min_uniform_buffer_offset_alignment)
        );
    }

    // Buffers.
    {
        mDrawCountBuffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32),
            .debug_name = "DrawCountBuffer",
        });

        mMeshPrimitiveVisibleBuffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debug_name = "MeshPrimitiveVisibleBuffer",
        });

        mDebugDrawCountBuffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * 1, // TODO: maybe enum max count for offsets?
            .debug_name = "DebugDrawCountBuffer",
        });

        mDebugDrawRectBuffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(DebugDrawRectData) * RENDERER_DEBUG_DRAW_RECT_MAX_COUNT,
            .debug_name = "DebugDrawRectBuffer",
        });

        mDebugDrawCmdBuffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(rhi::DrawIndirectCommand),
            .debug_name = "DebugDrawCmdBuffer",
        });
    }

    mShadowTexture = rhi::create_texture({
        .format = rhi::FORMAT_D16_UNORM,
        .type = rhi::TEXTURE_TYPE_2D_ARRAY,
        .dimensions = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS, 1},
        .layer_count = RENDERER_SHADOW_MAP_CASCADE_COUNT,
        .usage = rhi::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "ShadowTexture",
    });

    if (!RecompilePipelines())
    {
        return false;
    }

    // Synchronization primitives.
    {
        mFrameSemaphore = rhi::create_semaphore(0);

        for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i)
        {
            mFrame[i].startSemaphore.semaphore = rhi::create_semaphore(0);
            mFrame[i].shadowSemaphore.semaphore = rhi::create_semaphore(0);
            mFrame[i].ssaoSemaphore.semaphore = rhi::create_semaphore(0);
        }
    }

    // Shadow map resources.
    {
        // One texture descriptor per cascade.
        for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
        {
            mShadowTextureDescriptorCascade[i] = rhi::create_texture_descriptor({
                .texture = mShadowTexture,
                .type = rhi::TEXTURE_TYPE_2D,
                .base_layer = u32(i),
                .layer_count = 1,
            });
        }

        mShadowSampler = rhi::create_sampler({
            .compare_enable = true,
            .compare_op = rhi::COMPARE_OP_GREATER,
        });

        // PCF jitter offsets.
        {
            mShadowPcfJitterTexture = rhi::create_texture({
                .format = rhi::FORMAT_R8G8B8A8_SNORM,
                .type = rhi::TEXTURE_TYPE_3D,
                .dimensions =
                    {
                        RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
                        RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
                        RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U
                            * RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V / 2,
                    },
                .usage = rhi::TEXTURE_USAGE_TRANSFER_DST_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
                .debug_name = "ShadowPcfJitterTexture",
            });

            const std::vector<i8> jitterOffsets = CreateShadowJitterOffsets(
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U,
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V
            );

            const u64 uploadSize = VEC_SIZE_BYTES(jitterOffsets);
            rhi::Buffer stagingBuffer = rhi::create_buffer({
                .size = uploadSize,
                .debug_name = "StagingBuffer",
            });
            DEFER(rhi::destroy_buffer(stagingBuffer));

            memcpy(rhi::get_buffer_host_ptr(stagingBuffer), jitterOffsets.data(), uploadSize);

            rhi::CommandBuffer cb = rhi::create_command_buffer(rhi::QUEUE_GRAPHICS);
            DEFER(rhi::destroy_command_buffer(cb));

            rhi::begin_command_buffer(cb);

            rhi::cmd_texture_barrier(
                cb,
                {{
                    .texture = mShadowPcfJitterTexture,
                    .old_layout = rhi::TEXTURE_LAYOUT_UNDEFINED,
                    .new_layout = rhi::TEXTURE_LAYOUT_GENERAL,
                    .src_stage_mask = rhi::STAGE_HOST_BIT,
                    .dst_stage_mask = rhi::STAGE_ALL_TRANSFER_BIT,
                    .dst_access_mask = rhi::ACCESS_TRANSFER_WRITE_BIT,
                }}
            );

            rhi::cmd_copy_buffer_to_texture(
                cb,
                stagingBuffer,
                mShadowPcfJitterTexture,
                {{
                    .texture_dimensions = {
                        u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE),
                        u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE),
                        u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U
                            * RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V / 2),
                    },
                }}
            );

            rhi::cmd_barrier(
                cb,
                rhi::STAGE_ALL_TRANSFER_BIT,
                rhi::ACCESS_TRANSFER_WRITE_BIT,
                rhi::STAGE_FRAGMENT_SHADER_BIT,
                rhi::ACCESS_SHADER_READ_BIT
            );

            rhi::end_command_buffer(cb);

            rhi::queue_submit(rhi::QUEUE_GRAPHICS, {{.cb = cb}});

            rhi::device_wait_idle();

            mShadowPcfJitterSampler = rhi::create_sampler({
                .mag_filter = rhi::FILTER_NEAREST,
                .min_filter = rhi::FILTER_NEAREST,
                .mipmap_mode = rhi::SAMPLER_MIPMAP_MODE_NEAREST,
                .address_mode_u = rhi::SAMPLER_ADDRESS_MODE_REPEAT,
                .address_mode_v = rhi::SAMPLER_ADDRESS_MODE_REPEAT,
                .address_mode_w = rhi::SAMPLER_ADDRESS_MODE_REPEAT,
            });
        }
    }

    // Samplers.
    {
        mTextureSampler = rhi::create_sampler({
            .address_mode_u = rhi::SAMPLER_ADDRESS_MODE_REPEAT,
            .address_mode_v = rhi::SAMPLER_ADDRESS_MODE_REPEAT,
            .address_mode_w = rhi::SAMPLER_ADDRESS_MODE_REPEAT,
            .anisotropy_enable = true,
            .max_anisotropy = 4.0f,
            .max_lod = 16.0f,
        });

        mLinearSampler = rhi::create_sampler({});

        mNearestSampler = rhi::create_sampler({
            .mag_filter = rhi::FILTER_NEAREST,
            .min_filter = rhi::FILTER_NEAREST,
        });

        mMinSampler = rhi::create_sampler({
            .reduction_mode = rhi::SAMPLER_REDUCTION_MODE_MIN,
            .max_lod = 16.0f,
        });
    }

    // Scene.
    {
        const std::string gltfPath = "../assets/main_sponza/NewSponza_Main_glTF_003.gltf";

        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        std::vector<MeshPrimitive> meshPrimitives;
        std::vector<rhi::DrawIndexedIndirectCommand> drawCmds;
        std::vector<DrawData> drawData;
        std::vector<Material> materials;
        std::vector<std::string> texturePaths;

        if (!load_scene(
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

        mVertexBuffer = rhi::create_buffer({
            .size = VEC_SIZE_BYTES(vertices),
            .debug_name = "VertexBuffer",
        });
        memcpy(rhi::get_buffer_host_ptr(mVertexBuffer), vertices.data(), VEC_SIZE_BYTES(vertices));
        rhi::unmap_buffer(mVertexBuffer);

        mIndexBuffer = rhi::create_buffer({
            .size = VEC_SIZE_BYTES(indices),
            .debug_name = "IndexBuffer",
        });
        memcpy(rhi::get_buffer_host_ptr(mIndexBuffer), indices.data(), VEC_SIZE_BYTES(indices));
        rhi::unmap_buffer(mIndexBuffer);

        mDrawCmdBuffer1 = rhi::create_buffer({
            .size = sizeof(rhi::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debug_name = "DrawCmdBuffer1",
        });

        mDrawCmdEarlyBuffer2 = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(rhi::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debug_name = "DrawCmdEarlyBuffer2",
        });

        mDrawCmdLateBuffer2 = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(rhi::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debug_name = "DrawCmdLateBuffer2",
        });

        mDrawCmdShadowBuffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(rhi::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debug_name = "DrawCmdShadowBuffer",
        });

        mDrawIndicesEarlyBuffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debug_name = "DrawIndicesEarlyBuffer",
        });

        mDrawIndicesLateBuffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debug_name = "DrawIndicesLateBuffer",
        });

        mDrawIndicesShadowBuffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debug_name = "DrawIndicesShadowBuffer",
        });

        mMaterialBuffer = rhi::create_buffer({
            .size = sizeof(Material) * MAX_DRAW_CALLS,
            .debug_name = "MaterialBuffer",
        });

        mDrawDataBuffer = rhi::create_buffer({
            .size = sizeof(DrawData) * MAX_DRAW_CALLS,
            .debug_name = "DrawDataBuffer",
        });

        memcpy(
            rhi::get_buffer_host_ptr(mDrawCmdBuffer1),
            drawCmds.data(),
            VEC_SIZE_BYTES(drawCmds)
        );

        memcpy(
            rhi::get_buffer_host_ptr(mMaterialBuffer),
            materials.data(),
            VEC_SIZE_BYTES(materials)
        );

        memcpy(
            rhi::get_buffer_host_ptr(mDrawDataBuffer),
            drawData.data(),
            VEC_SIZE_BYTES(drawData)
        );
    }

    // Command buffers
    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i)
    {
        mFrame[i].startCommandBuffer = rhi::create_command_buffer(rhi::QUEUE_GRAPHICS, i, "Start");
        mFrame[i].shadowCommandBuffer
            = rhi::create_command_buffer(rhi::QUEUE_GRAPHICS, i, "Shadow");
        mFrame[i].ssaoCommandBuffer = rhi::create_command_buffer(rhi::QUEUE_COMPUTE, i, "SSAO");
        mFrame[i].endCommandBuffer = rhi::create_command_buffer(rhi::QUEUE_GRAPHICS, i, "End");
    }

    if (!mImguiRenderer.init(mWindow, rhi::get_texture_format(rhi::get_swapchain_texture(0))))
    {
        fprintf(stderr, "failed to initialize ImGui renderer\n");
        return false;
    }

    // Initializing resources.
    {
        rhi::CommandBuffer cb = rhi::create_command_buffer(rhi::QUEUE_GRAPHICS);
        DEFER(rhi::destroy_command_buffer(cb));

        rhi::begin_command_buffer(cb);

        rhi::cmd_fill_buffer(cb, mMeshPrimitiveVisibleBuffer, 0, sizeof(u32) * MAX_DRAW_CALLS, 0);

        rhi::end_command_buffer(cb);

        rhi::queue_submit(rhi::QUEUE_GRAPHICS, {{.cb = cb}});
        rhi::queue_wait_idle(rhi::QUEUE_GRAPHICS);
    }

    mSwapchainNeedsRecreating = true;
    mTaaJitterMaxIdx = 8;
    mTaaPassData.blendWeight = 0.1f;
    mUniformData.ambientIntensity = 0.04f;
    mUniformData.sunIntensity = 1.0f;
    mUniformData.gradErrorMax = 0.01f;
    mUniformData.enableSSAO = true;
    mUniformData.enableFog = true;

    mShadowPassData.enablePcf = 1;
    mShadowPassData.pcfKernelScale = 3.0f;
    mShadowPassData.pcfKernelCascadeScales[0] = 1.0f;
    mShadowPassData.pcfKernelCascadeScales[1] = 0.40f;
    mShadowPassData.pcfKernelCascadeScales[2] = 0.0f;
    mShadowPassData.pcfKernelCascadeScales[3] = 0.0f;
    mShadowPassData.normalOffset = 1.0f;
    mShadowPassData.constantOffset = 0.00003f;

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
    rhi::device_wait_idle();

    mImguiRenderer.cleanup();

    CleanupPipelines();
    CleanupColorResources();
    CleanupDepthResources();

    for (rhi::Texture& tex : mTextures)
    {
        rhi::destroy_texture(tex);
    }

    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        rhi::destroy_texture_descriptor(mShadowTextureDescriptorCascade[i]);
    }

    rhi::destroy_texture(mShadowTexture);
    rhi::destroy_texture(mShadowPcfJitterTexture);
    rhi::destroy_buffer(mDebugDrawCmdBuffer);
    rhi::destroy_buffer(mDebugDrawRectBuffer);
    rhi::destroy_buffer(mDebugDrawCountBuffer);
    rhi::destroy_buffer(mMeshPrimitiveVisibleBuffer);
    rhi::destroy_buffer(mDrawCountBuffer);
    rhi::destroy_buffer(mMaterialBuffer);
    rhi::destroy_buffer(mDrawDataBuffer);
    rhi::destroy_buffer(mVertexBuffer);
    rhi::destroy_buffer(mIndexBuffer);
    rhi::destroy_buffer(mDrawIndicesShadowBuffer);
    rhi::destroy_buffer(mDrawIndicesEarlyBuffer);
    rhi::destroy_buffer(mDrawIndicesLateBuffer);
    rhi::destroy_buffer(mDrawCmdShadowBuffer);
    rhi::destroy_buffer(mDrawCmdEarlyBuffer2);
    rhi::destroy_buffer(mDrawCmdLateBuffer2);
    rhi::destroy_buffer(mDrawCmdBuffer1);
    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i)
    {
        mFrame[i].uniformBufferManager.cleanup();
    }
    rhi::destroy_semaphore(mFrameSemaphore);
    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i)
    {
        rhi::destroy_semaphore(mFrame[i].startSemaphore.semaphore);
        rhi::destroy_semaphore(mFrame[i].shadowSemaphore.semaphore);
        rhi::destroy_semaphore(mFrame[i].ssaoSemaphore.semaphore);
    }
    rhi::destroy_sampler(mShadowPcfJitterSampler);
    rhi::destroy_sampler(mShadowSampler);
    rhi::destroy_sampler(mMinSampler);
    rhi::destroy_sampler(mNearestSampler);
    rhi::destroy_sampler(mLinearSampler);
    rhi::destroy_sampler(mTextureSampler);
    CleanupSwapchain();
    rhi::destroy();
}

bool Renderer::StartNewFrame()
{
    DEBUG_ASSERT(!mNewFrameStarted);

    if (mRenderingPaused)
    {
        return true;
    }

    if (mUniformData.frameCount >= rhi::FRAMES_IN_FLIGHT)
    {
        rhi::wait_semaphore(mFrameSemaphore, mUniformData.frameCount - rhi::FRAMES_IN_FLIGHT + 1);
    }

    rhi::begin_new_frame(mFrameIdx);

    mImguiRenderer.start_new_frame();

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
        CreateSwapchain(mWindowSize);
    }

    Frame& frame = mFrame[mFrameIdx];

    // I can't be arsed to handle the edge cases and it's only used for debugging.
    if (mRenderModeChanged)
    {
        rhi::device_wait_idle();

        rhi::CommandBuffer cb = rhi::create_command_buffer(rhi::QUEUE_GRAPHICS);
        DEFER(rhi::destroy_command_buffer(cb));

        rhi::begin_command_buffer(cb);

        rhi::cmd_barrier(
            cb,
            rhi::STAGE_ALL_COMMANDS_BIT,
            rhi::ACCESS_MEMORY_WRITE_BIT,
            rhi::STAGE_ALL_COMMANDS_BIT,
            rhi::ACCESS_MEMORY_READ_BIT | rhi::ACCESS_MEMORY_WRITE_BIT
        );

        rhi::end_command_buffer(cb);

        rhi::queue_submit(rhi::QUEUE_GRAPHICS, {{.cb = cb}});

        rhi::device_wait_idle();
    }

    rhi::Texture swapchainTextureHandle{};
    rhi::SwapchainResult swapchainResult
        = rhi::acquire_next_swapchain_texture(swapchainTextureHandle);

    if (swapchainResult == rhi::SWAPCHAIN_OUT_OF_DATE)
    {
        mSwapchainNeedsRecreating = true;
        return true;
    }
    else if (
        (swapchainResult != rhi::SWAPCHAIN_SUCCESS)
        && (swapchainResult != rhi::SWAPCHAIN_SUBOPTIMAL)
    )
    {
        fprintf(stderr, "RHI::AcquireNextSwapchainTexture failed\n");
        return false;
    }

    mUniformData.deltaTime = deltaTime;

    if ((mTaaPassData.enable == 1) && (mUniformData.renderMode != RENDER_MODE_GRAD_ERROR))
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

        mCullPassData.frustumPlaneXX = frustumPlaneX.X();
        mCullPassData.frustumPlaneXZ = frustumPlaneX.Z();
        mCullPassData.frustumPlaneYY = frustumPlaneY.Y();
        mCullPassData.frustumPlaneYZ = frustumPlaneY.Z();

        mUniformData.cullWorldToView = mUniformData.worldToView;
    }

    UpdateShadowCascades();

    frame.uniformBufferManager.on_new_frame();
    mUniformBufferDescriptor = frame.uniformBufferManager.push(&mUniformData, sizeof(mUniformData));
    mShadowPassUniformBufferDescriptor
        = frame.uniformBufferManager.push(&mShadowPassData, sizeof(mShadowPassData));

    switch (mUniformData.renderMode)
    {
    case RENDER_MODE_GRAD_ERROR:
        RecordAndSubmitDebugGradError(swapchainTextureHandle);
        break;
    default:
        RecordAndSubmitVisibility(swapchainTextureHandle);
        break;
    }

    swapchainResult = rhi::queue_present(rhi::QUEUE_GRAPHICS);
    if ((swapchainResult == rhi::SWAPCHAIN_OUT_OF_DATE)
        || (swapchainResult == rhi::SWAPCHAIN_SUBOPTIMAL))
    {
        mSwapchainNeedsRecreating = true;
    }
    else if (swapchainResult != rhi::SWAPCHAIN_SUCCESS)
    {
        fprintf(stderr, "RHI::QueuePresent failed\n");
        return false;
    }

    mNewFrameStarted = false;

    mPrevFrameIdx = mFrameIdx;
    mFrameIdx = (mFrameIdx + 1) % rhi::FRAMES_IN_FLIGHT;
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
    rhi::destroy_pipeline(mShadowCullPipeline);
    rhi::destroy_pipeline(mShadowPipeline);
    rhi::destroy_pipeline(mBlurFogPipeline);
    rhi::destroy_pipeline(mFogPipeline);
    rhi::destroy_pipeline(mAmbientOcclusionUpsamplePipeline);
    rhi::destroy_pipeline(mAmbientOcclusionBlurPipeline);
    rhi::destroy_pipeline(mAmbientOcclusionPipeline);
    rhi::destroy_pipeline(mDepthViewQuarterResPipeline);
    rhi::destroy_pipeline(mDebugDrawFillCmdPipeline);
    rhi::destroy_pipeline(mDebugDrawRectPipeline);
    rhi::destroy_pipeline(mDepthReducePipeline);
    rhi::destroy_pipeline(mDebugGradErrorPipeline);
    rhi::destroy_pipeline(mTaaResolvePipeline);
    rhi::destroy_pipeline(mCullLatePipeline);
    rhi::destroy_pipeline(mCullEarlyPipeline);
    rhi::destroy_pipeline(mFullscreenPipeline);
    rhi::destroy_pipeline(mVisibilityRenderPipeline);
    rhi::destroy_pipeline(mVisibilityPipeline);
}

bool Renderer::RecompilePipelines()
{
    rhi::device_wait_idle();

    CleanupPipelines();

    // Graphics pipelines
    {
        utils::FileData vertData = utils::file_read("visibility_buffer.vert.hlsl.spv");
        DEFER(free(vertData.data));
        utils::FileData fragData = utils::file_read("visibility_buffer.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mVisibilityPipeline = rhi::create_graphics_pipeline({
            .bytecodes =
                {
                    {static_cast<u8*>(vertData.data), int(vertData.size)},
                    {static_cast<u8*>(fragData.data), int(fragData.size)},
                },
            .cull = rhi::CULL_CW,
            .depth_format = rhi::get_texture_format(mDepthTexture),
            .depth_mask = rhi::DEPTH_READ_BIT | rhi::DEPTH_WRITE_BIT,
            .color_targets =
                {
                    {.format = rhi::get_texture_format(mVisibilityTexture),
                     .color_component_mask = rhi::COLOR_COMPONENT_R_BIT | rhi::COLOR_COMPONENT_G_BIT},
                },
            .debug_name = "visibility_buffer_pass",
        });
        if (!mVisibilityPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData vertData = utils::file_read("debug_draw_rect.vert.hlsl.spv");
        DEFER(free(vertData.data));
        utils::FileData fragData = utils::file_read("debug_draw_rect.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mDebugDrawRectPipeline = rhi::create_graphics_pipeline({
            .bytecodes =
                {
                    {static_cast<u8*>(vertData.data), int(vertData.size)},
                    {static_cast<u8*>(fragData.data), int(fragData.size)},
                },
            .color_targets =
                {
                    {.format = rhi::get_texture_format(mRenderTexture)},
                },
            .debug_name = "DebugDrawRectPass",
        });
        if (!mDebugDrawRectPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData vertData = utils::file_read("fullscreen.vert.hlsl.spv");
        DEFER(free(vertData.data));
        utils::FileData fragData = utils::file_read("fullscreen.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mFullscreenPipeline = rhi::create_graphics_pipeline({
            .bytecodes =
                {
                    {static_cast<u8*>(vertData.data), int(vertData.size)},
                    {static_cast<u8*>(fragData.data), int(fragData.size)},
                },
            .color_targets = {{.format = rhi::get_texture_format(rhi::get_swapchain_texture(0))}},
            .debug_name = "FullscreenPass",
        });
        if (!mFullscreenPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData vertData = utils::file_read("debug_grad_error.vert.hlsl.spv");
        DEFER(free(vertData.data));
        utils::FileData fragData = utils::file_read("debug_grad_error.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mDebugGradErrorPipeline = rhi::create_graphics_pipeline({
            .bytecodes =
                {
                    {static_cast<u8*>(vertData.data), int(vertData.size)},
                    {static_cast<u8*>(fragData.data), int(fragData.size)},
                },
            .cull = rhi::CULL_CW,
            .depth_format = rhi::get_texture_format(mDepthTexture),
            .depth_mask = rhi::DEPTH_READ_BIT | rhi::DEPTH_WRITE_BIT,
            .color_targets = {{.format = rhi::get_texture_format(rhi::get_swapchain_texture(0))}},
            .debug_name = "DebugGradErrorPass",
        });
        if (!mDebugGradErrorPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData vertData = utils::file_read("shadow.vert.hlsl.spv");
        DEFER(free(vertData.data));
        utils::FileData fragData = utils::file_read("shadow.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mShadowPipeline = rhi::create_graphics_pipeline({
            .bytecodes =
                {
                    {static_cast<u8*>(vertData.data), int(vertData.size)},
                    {static_cast<u8*>(fragData.data), int(fragData.size)},
                },
            .cull = rhi::CULL_CW,
            .depth_format = rhi::get_texture_format(mShadowTexture),
            .depth_mask = rhi::DEPTH_READ_BIT | rhi::DEPTH_WRITE_BIT,
            .depth_clamp_enable = true,
            .debug_name = "ShadowPass",
        });
        if (!mShadowPipeline)
        {
            return false;
        }
    }

    // Compute pipelines.
    {
        utils::FileData compData = utils::file_read("cull_early.comp.hlsl.spv");
        DEFER(free(compData.data));

        mCullEarlyPipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "CullEarlyPass",
        });
        if (!mCullEarlyPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("cull_late.comp.hlsl.spv");
        DEFER(free(compData.data));

        mCullLatePipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "CullLatePass",
        });
        if (!mCullLatePipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("shadow_cull.comp.hlsl.spv");
        DEFER(free(compData.data));

        mShadowCullPipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "ShadowCullPass",
        });
        if (!mCullLatePipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("visibility_render.comp.hlsl.spv");
        DEFER(free(compData.data));

        mVisibilityRenderPipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .uses_bindless_textures = true,
            .debug_name = "VisibilityRenderPass",
        });
        if (!mVisibilityRenderPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("taa_resolve.comp.hlsl.spv");
        DEFER(free(compData.data));

        mTaaResolvePipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "TaaResolvePass",
        });
        if (!mTaaResolvePipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("depth_reduce.comp.hlsl.spv");
        DEFER(free(compData.data));

        mDepthReducePipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "DepthReducePass",
        });
        if (!mDepthReducePipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("depth_view_quarter_res.comp.hlsl.spv");
        DEFER(free(compData.data));

        mDepthViewQuarterResPipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "DepthViewQuarterResPass",
        });
        if (!mDepthViewQuarterResPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("ssao.comp.hlsl.spv");
        DEFER(free(compData.data));

        mAmbientOcclusionPipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "AmbientOcclusionPass",
        });
        if (!mAmbientOcclusionPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("blur_ssao.comp.hlsl.spv");
        DEFER(free(compData.data));

        mAmbientOcclusionBlurPipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "AmbientOcclusionBlurPass",
        });
        if (!mAmbientOcclusionBlurPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("upsample_ssao.comp.hlsl.spv");
        DEFER(free(compData.data));

        mAmbientOcclusionUpsamplePipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "AmbientOcclusionUpsamplePass",
        });
        if (!mAmbientOcclusionUpsamplePipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("fog.comp.hlsl.spv");
        DEFER(free(compData.data));

        mFogPipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "FogPass",
        });
        if (!mFogPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("blur_fog.comp.hlsl.spv");
        DEFER(free(compData.data));

        mBlurFogPipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "BlurFogPass",
        });
        if (!mBlurFogPipeline)
        {
            return false;
        }
    }

    {
        utils::FileData compData = utils::file_read("debug_draw_fill_cmd.comp.hlsl.spv");
        DEFER(free(compData.data));

        mDebugDrawFillCmdPipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(compData.data), int(compData.size)},
            .debug_name = "DebugDrawFillCmdPass",
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

    mUniformData.sunColor = CalcSunColor(mUniformData.sunDirectionWorld);
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

        const rhi::Format format = FormatToRHI(ktxTexture2_GetVkFormat(ktxTex));
        const Vec3U32 extent{ktxTex->baseWidth, ktxTex->baseHeight, ktxTex->baseDepth};
        const u32 mipLevels = ktxTex->numLevels;
        const ktx_size_t size = ktxTexture_GetDataSize(ktxTexture(ktxTex));
        const ktx_uint8_t* ktxData = ktxTexture_GetData(ktxTexture(ktxTex));

        mTextures[i] = rhi::create_texture({
            .format = format,
            .dimensions = {ktxTex->baseWidth, ktxTex->baseHeight, 1},
            .mip_count = mipLevels,
            .usage = rhi::TEXTURE_USAGE_SAMPLED_BIT | rhi::TEXTURE_USAGE_TRANSFER_DST_BIT,
            .debug_name = texturePaths[i].c_str(),
        });

        rhi::Buffer stagingBuffer = rhi::create_buffer({
            .size = size,
            .debug_name = "StagingBuffer",
        });
        DEFER(rhi::destroy_buffer(stagingBuffer));

        std::vector<rhi::BufferTextureCopy> copyRegions(mipLevels);

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
                .buffer_offset = offset,
                .texture_subresource = {.mip_level = mipLevel},
                .texture_dimensions = {extent.x >> mipLevel, extent.y >> mipLevel, 1},
            };
        }
        memcpy(rhi::get_buffer_host_ptr(stagingBuffer), ktxData, size);
        rhi::unmap_buffer(stagingBuffer);

        rhi::CommandBuffer cb = rhi::create_command_buffer(rhi::QUEUE_GRAPHICS);
        DEFER(rhi::destroy_command_buffer(cb));

        rhi::begin_command_buffer(cb);

        rhi::cmd_texture_barrier(
            cb,
            {{
                .texture = mTextures[i],
                .old_layout = rhi::TEXTURE_LAYOUT_UNDEFINED,
                .new_layout = rhi::TEXTURE_LAYOUT_GENERAL,
                .dst_stage_mask = rhi::STAGE_TRANSFER_BIT,
                .dst_access_mask = rhi::ACCESS_TRANSFER_WRITE_BIT,
                .mip_count = mipLevels,
            }}
        );

        rhi::cmd_copy_buffer_to_texture(
            cb,
            stagingBuffer,
            mTextures[i],
            {copyRegions.data(), int(copyRegions.size())}
        );

        rhi::end_command_buffer(cb);

        rhi::queue_submit(rhi::QUEUE_GRAPHICS, {{.cb = cb}});
        rhi::queue_wait_idle(rhi::QUEUE_GRAPHICS);
    }

    for (size_t i = 0; i < mTextures.size(); ++i)
    {
        rhi::update_texture_descriptor_set(mTextures[i], u32(i));
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

        mShadowPassData.texelSizes[i] = sphereDiameter / RENDERER_SHADOW_MAP_DIMENSIONS;
        mShadowPassData.worldToClip[i] = lightProjectionMatrix * lightViewMatrix;
    }
}

void Renderer::VisibilityBufferPass(rhi::CommandBuffer cb, bool cullLate)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mVisibilityPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
            cullLate ? mDrawIndicesLateBuffer : mDrawIndicesEarlyBuffer,
            mDrawDataBuffer,
            mVertexBuffer,
        }
    );

    const PushConstantsVisibilityBuffer pushConstants = {
        .cullLate = cullLate,
    };
    rhi::cmd_push_constants(cb, pipeline, &pushConstants);

    const Vec3U32 renderDim = rhi::get_texture_dimensions(mRenderTexture);

    rhi::cmd_set_viewport({
        .cb = cb,
        .y = f32(renderDim.y),
        .width = f32(renderDim.x),
        .height = -f32(renderDim.y),
    });

    rhi::cmd_set_scissor({.cb = cb, .extent = renderDim.xy()});

    rhi::cmd_begin_rendering({
        .cb = cb,
        .extent = rhi::get_texture_dimensions(mRenderTexture).xy(),
        .color_targets = {{
            .attachment = mVisibilityTexture,
            .load_op = cullLate ? rhi::ATTACHMENT_LOAD_OP_LOAD : rhi::ATTACHMENT_LOAD_OP_CLEAR,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        }},
        .depth_target = {
            .attachment = mDepthTexture,
            .load_op = cullLate ? rhi::ATTACHMENT_LOAD_OP_LOAD : rhi::ATTACHMENT_LOAD_OP_CLEAR,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        },
    });

    rhi::cmd_bind_index_buffer(cb, mIndexBuffer, 0, rhi::INDEX_TYPE_U32);

    rhi::cmd_draw_indexed_indirect_count(
        cb,
        cullLate ? mDrawCmdLateBuffer2 : mDrawCmdEarlyBuffer2,
        0,
        mDrawCountBuffer,
        0,
        mUniformData.drawCount
    );

    rhi::cmd_end_rendering(cb);
}

void Renderer::CullPass(rhi::CommandBuffer cb, bool late)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = late ? mCullLatePipeline : mCullEarlyPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_constants(cb, pipeline, &mCullPassData);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
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

    rhi::cmd_dispatch(
        cb,
        {CalcDispatchSize(mUniformData.drawCount, rhi::get_pipeline_local_size(pipeline).x), 1, 1}
    );
}

void Renderer::DepthReducePass(rhi::CommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mDepthReducePipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    PushConstantsDepthReduce pushConstants{};

    // In, out.
    rhi::DescriptorInfo descInfos[] = {
        mDepthTexture,
        mDepthPyramidMipTextureDescriptors[0],
        mMinSampler,
    };

    const Vec2U32 depthPyramidSize = rhi::get_texture_dimensions(mDepthPyramidTexture).xy();

    for (size_t i = 0; i < mDepthPyramidMipTextureDescriptors.size(); ++i)
    {
        descInfos[1].resource.texture_descriptor = mDepthPyramidMipTextureDescriptors[i];

        const u32 width = Max(1U, depthPyramidSize.x >> i);
        const u32 height = Max(1U, depthPyramidSize.y >> i);

        pushConstants.outWidth = width;
        pushConstants.outHeight = height;
        pushConstants.mipLevel = u32(i);

        rhi::cmd_push_constants(cb, pipeline, &pushConstants);

        rhi::cmd_push_descriptors(cb, pipeline, descInfos);

        rhi::cmd_dispatch(
            cb,
            CalcDispatchSize({width, height}, rhi::get_pipeline_local_size(pipeline))
        );

        rhi::cmd_barrier(
            cb,
            rhi::STAGE_COMPUTE_SHADER_BIT,
            rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
            rhi::STAGE_COMPUTE_SHADER_BIT,
            rhi::ACCESS_SHADER_SAMPLED_READ_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
        );

        descInfos[0].type = rhi::DescriptorInfo::TYPE_TEXTURE_DESCRIPTOR;
        descInfos[0].resource.texture_descriptor = mDepthPyramidMipTextureDescriptors[i];
    }
}

void Renderer::DepthViewQuarterResPass(rhi::CommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mDepthViewQuarterResPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_constants(cb, pipeline, &mSsaoPassData);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mDepthTexture,
            mDepthViewQuarterResTexture,
        }
    );

    CmdDispatchOverTextureSize(cb, pipeline, mDepthViewQuarterResTexture);
}

void Renderer::AmbientOcclusionPass(rhi::CommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mAmbientOcclusionPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_constants(cb, pipeline, &mSsaoPassData);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
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

void Renderer::ShadowCullPass(rhi::CommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mShadowCullPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    const PushConstantsShadow pushConstants = {
        .shadowCascadeIdx = 0,
        .renderPassFlags = RENDER_PASS_OPAQUE_BIT,
    };
    rhi::cmd_push_constants(cb, pipeline, &pushConstants);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
            mDrawDataBuffer,
            mDrawCountBuffer,
            mDrawCmdBuffer1,
            mDrawCmdShadowBuffer,
            mDrawIndicesShadowBuffer,
        }
    );

    rhi::cmd_dispatch(
        cb,
        {CalcDispatchSize(mUniformData.drawCount, RENDERER_CULL_WORKGROUP_SIZE), 1, 1}
    );
}

void Renderer::ShadowPass(rhi::CommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mShadowPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
            mShadowPassUniformBufferDescriptor,
            mDrawIndicesShadowBuffer,
            mDrawDataBuffer,
            mVertexBuffer,
        }
    );

    rhi::cmd_set_viewport({
        .cb = cb,
        .width = f32(RENDERER_SHADOW_MAP_DIMENSIONS),
        .height = f32(RENDERER_SHADOW_MAP_DIMENSIONS),
    });

    rhi::cmd_set_scissor({
        .cb = cb,
        .extent = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS},
    });

    rhi::cmd_bind_index_buffer(cb, mIndexBuffer, 0, rhi::INDEX_TYPE_U32);

    // TODO: check out VK_KHR_multiview.
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        const PushConstantsShadow pushConstants = {
            .shadowCascadeIdx = i,
            .renderPassFlags = RENDER_PASS_OPAQUE_BIT,
        };
        rhi::cmd_push_constants(cb, mShadowCullPipeline, &pushConstants);

        rhi::cmd_begin_rendering({
            .cb = cb,
            .extent = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS},
            .depth_target = {
                .attachment = mShadowTextureDescriptorCascade[i],
                .load_op = rhi::ATTACHMENT_LOAD_OP_CLEAR,
                .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
            },
        });

        rhi::cmd_draw_indexed_indirect_count(
            cb,
            mDrawCmdShadowBuffer,
            0,
            mDrawCountBuffer,
            0,
            mUniformData.drawCount
        );

        rhi::cmd_end_rendering(cb);
    }
}

void Renderer::FogPass(rhi::CommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mFogPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
            mShadowPassUniformBufferDescriptor,
            mNearestSampler,
            mDepthTexture,
            mShadowTexture,
            mFogTexture,
        }
    );

    CmdDispatchOverTextureSize(cb, pipeline, mFogTexture);
}

void Renderer::BlurFogPass(rhi::CommandBuffer cb, bool horizontal)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mBlurFogPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
            horizontal ? mFogTexture : mFogBlurredHorizontalTexture,
            horizontal ? mFogBlurredHorizontalTexture : mFogBlurredVerticalTexture,
        }
    );

    const PushConstantsFogBlur pushConstants = {.horizontal = horizontal};
    rhi::cmd_push_constants(cb, pipeline, &pushConstants);

    CmdDispatchOverTextureSize(cb, pipeline, mFogBlurredHorizontalTexture);
}

void Renderer::AmbientOcclusionBlurPass(rhi::CommandBuffer cb, bool horizontal)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mAmbientOcclusionBlurPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_constants(cb, pipeline, &mSsaoPassData);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mDepthViewQuarterResTexture,
            horizontal ? mAmbientOcclusionTexture : mAmbientOcclusionBlurredHorizontalTexture,
            horizontal ? mAmbientOcclusionBlurredHorizontalTexture
                       : mAmbientOcclusionBlurredVerticalTexture,
        }
    );

    mSsaoPassData.pixelOffsetX = horizontal ? 1 : 0;
    mSsaoPassData.pixelOffsetY = horizontal ? 0 : 1;
    rhi::cmd_push_constants(cb, pipeline, &mSsaoPassData);

    CmdDispatchOverTextureSize(cb, pipeline, mAmbientOcclusionBlurredHorizontalTexture);
}

void Renderer::AmbientOcclusionUpsamplePass(rhi::CommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mAmbientOcclusionUpsamplePipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
            mNearestSampler,
            mDepthTexture,
            mDepthViewQuarterResTexture,
            mAmbientOcclusionBlurredVerticalTexture,
            mAmbientOcclusionUpsampledTexture,
        }
    );

    CmdDispatchOverTextureSize(cb, pipeline, mAmbientOcclusionUpsampledTexture);
}

void Renderer::RenderPass(rhi::CommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mVisibilityRenderPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
            mShadowPassUniformBufferDescriptor,
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

    rhi::cmd_bind_texture_descriptor_set(cb, pipeline);

    CmdDispatchOverTextureSize(cb, pipeline, mRenderTexture);
}

void Renderer::TaaResolvePass(rhi::CommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = mTaaResolvePipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_constants(cb, pipeline, &mTaaPassData);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
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

void Renderer::DebugDrawPass(rhi::CommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    rhi::cmd_bind_pipeline(cb, mDebugDrawFillCmdPipeline);

    rhi::cmd_push_descriptors(
        cb,
        mDebugDrawFillCmdPipeline,
        {
            mDebugDrawCountBuffer,
            mDebugDrawCmdBuffer,
        }
    );

    rhi::cmd_dispatch(cb, {1, 1, 1});

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_DRAW_INDIRECT_BIT,
        rhi::ACCESS_INDIRECT_COMMAND_READ_BIT
    );

    rhi::cmd_bind_pipeline(cb, mDebugDrawRectPipeline);

    rhi::cmd_push_descriptors(
        cb,
        mDebugDrawRectPipeline,
        {
            mUniformBufferDescriptor,
            mDebugDrawCountBuffer,
            mDebugDrawRectBuffer,
        }
    );

    const Vec2U32 size = rhi::get_texture_dimensions(rhi::get_swapchain_texture(0)).xy();

    rhi::cmd_set_viewport({
        .cb = cb,
        .width = f32(size.x),
        .height = f32(size.y),
    });

    rhi::cmd_set_scissor({.cb = cb, .extent = size});

    rhi::cmd_begin_rendering({
        .cb = cb,
        .extent = size,
        .color_targets = {{
            .attachment = mFrame[mFrameIdx].resolvedRenderTexture,
            .load_op = rhi::ATTACHMENT_LOAD_OP_LOAD,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        }},
    });

    rhi::cmd_draw_indirect(cb, mDebugDrawCmdBuffer, 0, 1, 0);

    rhi::cmd_end_rendering(cb);
}

void Renderer::FullscreenPass(rhi::CommandBuffer cb, rhi::Texture swapchainTexture)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(swapchainTexture);

    const rhi::Pipeline pipeline = mFullscreenPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mFrame[mFrameIdx].resolvedRenderTexture,
            mLinearSampler,
        }
    );

    const Vec2U32 size = rhi::get_texture_dimensions(swapchainTexture).xy();

    rhi::cmd_set_viewport({
        .cb = cb,
        .y = f32(size.y),
        .width = f32(size.x),
        .height = -f32(size.y),
    });

    rhi::cmd_set_scissor({.cb = cb, .extent = size});

    rhi::cmd_begin_rendering({
        .cb = cb,
        .extent = size,
        .color_targets = {{
            .attachment = swapchainTexture,
            .load_op = rhi::ATTACHMENT_LOAD_OP_LOAD,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        }},
    });

    rhi::cmd_draw(cb, 3, 1, 0, 0);

    rhi::cmd_end_rendering(cb);
}

void Renderer::DebugDrawGradErrorPass(
    rhi::CommandBuffer cb,
    bool cullLate,
    rhi::Texture swapchainTexture
)
{
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(swapchainTexture);

    const rhi::Pipeline pipeline = mDebugGradErrorPipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            mUniformBufferDescriptor,
            cullLate ? mDrawIndicesLateBuffer : mDrawIndicesEarlyBuffer,
            cullLate ? mDrawCmdLateBuffer2 : mDrawCmdEarlyBuffer2,
            mDrawDataBuffer,
            mIndexBuffer,
            mVertexBuffer,
        }
    );

    const Vec2U32 size = rhi::get_texture_dimensions(swapchainTexture).xy();

    rhi::cmd_set_viewport({
        .cb = cb,
        .y = f32(size.y),
        .width = f32(size.x),
        .height = -f32(size.y),
    });

    rhi::cmd_set_scissor({.cb = cb, .extent = size});

    rhi::cmd_begin_rendering({
        .cb = cb,
        .extent = size,
        .color_targets = {{
            .attachment = swapchainTexture,
            .load_op = cullLate ? rhi::ATTACHMENT_LOAD_OP_LOAD : rhi::ATTACHMENT_LOAD_OP_CLEAR,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        }},
        .depth_target = {
            .attachment = mDepthTexture,
            .load_op = cullLate ? rhi::ATTACHMENT_LOAD_OP_LOAD : rhi::ATTACHMENT_LOAD_OP_CLEAR,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        },
    });

    rhi::cmd_bind_index_buffer(cb, mIndexBuffer, 0, rhi::INDEX_TYPE_U32);

    rhi::cmd_draw_indexed_indirect_count(
        cb,
        cullLate ? mDrawCmdLateBuffer2 : mDrawCmdEarlyBuffer2,
        0,
        mDrawCountBuffer,
        0,
        mUniformData.drawCount
    );

    rhi::cmd_end_rendering(cb);
}

void Renderer::RecordAndSubmitDebugGradError(rhi::Texture swapchainTexture)
{
    DEBUG_ASSERT(swapchainTexture);

    const rhi::CommandBuffer cb = mFrame[mFrameIdx].startCommandBuffer;

    rhi::begin_command_buffer(cb);

    rhi::cmd_texture_invalidate_barrier(
        cb,
        rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | rhi::STAGE_LATE_FRAGMENT_TESTS_BIT,
        rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | rhi::STAGE_COMPUTE_SHADER_BIT
            | rhi::STAGE_EARLY_FRAGMENT_TESTS_BIT,
        rhi::ACCESS_COLOR_ATTACHMENT_WRITE_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
            | rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        {
            mVisibilityTexture,
            mDepthPyramidTexture,
            swapchainTexture,
            mDepthTexture,
        }
    );

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_DRAW_INDIRECT_BIT,
        rhi::ACCESS_NONE,
        rhi::STAGE_TRANSFER_BIT,
        rhi::ACCESS_NONE
    );

    rhi::cmd_fill_buffer(cb, mDrawCountBuffer, 0, sizeof(u32), 0);

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_TRANSFER_BIT | rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_FRAGMENT_SHADER_BIT,
        rhi::ACCESS_TRANSFER_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    CullPass(cb, false);

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_DRAW_INDIRECT_BIT | rhi::STAGE_VERTEX_SHADER_BIT
            | rhi::STAGE_FRAGMENT_SHADER_BIT,
        rhi::ACCESS_INDIRECT_COMMAND_READ_BIT | rhi::ACCESS_SHADER_STORAGE_READ_BIT
    );

    DebugDrawGradErrorPass(cb, false, swapchainTexture);

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_LATE_FRAGMENT_TESTS_BIT,
        rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    if (!mCullCameraFrozen)
    {
        DepthReducePass(cb);
    }

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_DRAW_INDIRECT_BIT,
        rhi::ACCESS_NONE,
        rhi::STAGE_COMPUTE_SHADER_BIT | rhi::STAGE_TRANSFER_BIT,
        rhi::ACCESS_NONE
    );

    rhi::cmd_fill_buffer(cb, mDrawCountBuffer, 0, sizeof(u32), 0);

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_TRANSFER_BIT | rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_FRAGMENT_SHADER_BIT
            | rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_TRANSFER_WRITE_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_SHADER_SAMPLED_READ_BIT
            | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    CullPass(cb, true);

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_DRAW_INDIRECT_BIT | rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_FRAGMENT_SHADER_BIT
            | rhi::STAGE_COMPUTE_SHADER_BIT | rhi::STAGE_EARLY_FRAGMENT_TESTS_BIT,
        rhi::ACCESS_INDIRECT_COMMAND_READ_BIT | rhi::ACCESS_SHADER_STORAGE_READ_BIT
            | rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
    );

    DebugDrawGradErrorPass(cb, true, swapchainTexture);

    mImguiRenderer.update_vertex_index_buffers(u32(mFrameIdx));

    // TODO: separate pass.
    if (mEnableUI)
    {
        const Vec2U32 size = rhi::get_texture_dimensions(swapchainTexture).xy();

        rhi::cmd_begin_rendering({
            .cb = cb,
            .extent = size,
            .color_targets = {{
                .attachment = swapchainTexture,
                .load_op = rhi::ATTACHMENT_LOAD_OP_LOAD,
                .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
            }},
        });

        mImguiRenderer.render(cb, u32(mFrameIdx));

        rhi::cmd_end_rendering(cb);
    }

    rhi::cmd_texture_barrier(
        cb,
        {{
            swapchainTexture,
            rhi::TEXTURE_LAYOUT_GENERAL,
            rhi::TEXTURE_LAYOUT_PRESENT_SRC,
            rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            rhi::ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            rhi::STAGE_NONE,
            rhi::ACCESS_NONE,
        }}
    );

    rhi::end_command_buffer(cb);

    mFrame[mFrameIdx].uniformBufferManager.flush();
    rhi::queue_submit(
        rhi::QUEUE_GRAPHICS,
        {{
            .cb = cb,
            .wait_for_texture_acquire = true,
            .signal_ready_to_present = true,
            .signal_semaphores = {
                {mFrameSemaphore, mUniformData.frameCount + 1},
            },
        }}
    );
}

void Renderer::RecordAndSubmitVisibility(rhi::Texture swapchainTexture)
{
    Frame& frame = mFrame[mFrameIdx];

    const rhi::CommandBuffer cbStart = frame.startCommandBuffer;
    rhi::begin_command_buffer(cbStart);

    rhi::cmd_texture_invalidate_barrier(
        cbStart,
        rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        rhi::ACCESS_NONE,
        rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | rhi::STAGE_EARLY_FRAGMENT_TESTS_BIT
            | rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_COLOR_ATTACHMENT_WRITE_BIT | rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
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

    rhi::cmd_fill_buffer(cbStart, mDrawCountBuffer, 0, sizeof(u32), 0);
    rhi::cmd_fill_buffer(cbStart, mDebugDrawCountBuffer, 0, sizeof(u32), 0);

    rhi::cmd_barrier(
        cbStart,
        rhi::STAGE_TRANSFER_BIT,
        rhi::ACCESS_TRANSFER_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    CullPass(cbStart, false);

    rhi::cmd_barrier(
        cbStart,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_DRAW_INDIRECT_BIT | rhi::STAGE_VERTEX_SHADER_BIT,
        rhi::ACCESS_INDIRECT_COMMAND_READ_BIT | rhi::ACCESS_SHADER_STORAGE_READ_BIT
    );

    VisibilityBufferPass(cbStart, false);

    rhi::cmd_barrier(
        cbStart,
        rhi::STAGE_LATE_FRAGMENT_TESTS_BIT,
        rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    if (!mCullCameraFrozen)
    {
        DepthReducePass(cbStart);
    }

    rhi::cmd_barrier(
        cbStart,
        rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_DRAW_INDIRECT_BIT,
        rhi::ACCESS_NONE,
        rhi::STAGE_COMPUTE_SHADER_BIT | rhi::STAGE_TRANSFER_BIT,
        rhi::ACCESS_NONE
    );

    rhi::cmd_fill_buffer(cbStart, mDrawCountBuffer, 0, sizeof(u32), 0);

    rhi::cmd_barrier(
        cbStart,
        rhi::STAGE_TRANSFER_BIT | rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_FRAGMENT_SHADER_BIT
            | rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_TRANSFER_WRITE_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_SHADER_SAMPLED_READ_BIT
            | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    CullPass(cbStart, true);

    rhi::cmd_barrier(
        cbStart,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_DRAW_INDIRECT_BIT | rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_COMPUTE_SHADER_BIT
            | rhi::STAGE_EARLY_FRAGMENT_TESTS_BIT,
        rhi::ACCESS_INDIRECT_COMMAND_READ_BIT | rhi::ACCESS_SHADER_STORAGE_READ_BIT
            | rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
    );

    VisibilityBufferPass(cbStart, true);

    rhi::end_command_buffer(cbStart);

    // TODO: I don't see why can't I use 2 submits per frame (1 for each queue).
    frame.uniformBufferManager.flush();
    rhi::queue_submit(
        rhi::QUEUE_GRAPHICS,
        {{
            .cb = cbStart,
            .wait_for_texture_acquire = true,
            .signal_semaphores = {
                {frame.startSemaphore.semaphore, frame.startSemaphore.Inc()},
            },
        }}
    );

    const rhi::CommandBuffer cbSSAO = frame.ssaoCommandBuffer;
    rhi::begin_command_buffer(cbSSAO);

    DepthViewQuarterResPass(cbSSAO);

    rhi::cmd_barrier(
        cbSSAO,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    AmbientOcclusionPass(cbSSAO);

    rhi::cmd_barrier(
        cbSSAO,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    AmbientOcclusionBlurPass(cbSSAO, true);

    rhi::cmd_barrier(
        cbSSAO,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    AmbientOcclusionBlurPass(cbSSAO, false);

    rhi::cmd_barrier(
        cbSSAO,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    AmbientOcclusionUpsamplePass(cbSSAO);

    rhi::end_command_buffer(cbSSAO);

    frame.uniformBufferManager.flush();
    rhi::queue_submit(
        rhi::QUEUE_COMPUTE,
        {{
            .cb = cbSSAO,
            .wait_semaphores =
                {
                    {frame.startSemaphore.semaphore, frame.startSemaphore.value},
                },
            .signal_semaphores = {
                {frame.ssaoSemaphore.semaphore, frame.ssaoSemaphore.Inc()},
            },
        }}
    );

    const rhi::CommandBuffer cbShadow = frame.shadowCommandBuffer;
    rhi::begin_command_buffer(cbShadow);

    rhi::cmd_fill_buffer(cbShadow, mDrawCountBuffer, 0, sizeof(u32), 0);

    rhi::cmd_barrier(
        cbShadow,
        rhi::STAGE_TRANSFER_BIT,
        rhi::ACCESS_TRANSFER_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    ShadowCullPass(cbShadow);

    rhi::cmd_barrier(
        cbShadow,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_DRAW_INDIRECT_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_INDIRECT_COMMAND_READ_BIT
    );

    ShadowPass(cbShadow);

    rhi::end_command_buffer(cbShadow);

    frame.uniformBufferManager.flush();
    rhi::queue_submit(
        rhi::QUEUE_GRAPHICS,
        {{
            .cb = cbShadow,
            .wait_semaphores =
                {
                    {frame.startSemaphore.semaphore, frame.startSemaphore.value},
                },
            .signal_semaphores = {
                {frame.shadowSemaphore.semaphore, frame.shadowSemaphore.Inc()},
            },
        }}
    );

    const rhi::CommandBuffer cbEnd = frame.endCommandBuffer;
    rhi::begin_command_buffer(cbEnd);

    FogPass(cbEnd);

    rhi::cmd_barrier(
        cbEnd,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    BlurFogPass(cbEnd, true);

    rhi::cmd_barrier(
        cbEnd,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    BlurFogPass(cbEnd, false);

    rhi::cmd_barrier(
        cbEnd,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    RenderPass(cbEnd);

    rhi::cmd_barrier(
        cbEnd,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    TaaResolvePass(cbEnd);

    rhi::cmd_barrier(
        cbEnd,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_FRAGMENT_SHADER_BIT | rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT | rhi::ACCESS_COLOR_ATTACHMENT_READ_BIT
    );

    DebugDrawPass(cbEnd);

    rhi::cmd_barrier(
        cbEnd,
        rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        rhi::ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        rhi::STAGE_FRAGMENT_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    FullscreenPass(cbEnd, swapchainTexture);

    mImguiRenderer.update_vertex_index_buffers(u32(mFrameIdx));

    if (mEnableUI)
    {
        const Vec2U32 size = rhi::get_texture_dimensions(swapchainTexture).xy();

        rhi::cmd_begin_rendering({
            .cb = cbEnd,
            .extent = size,
            .color_targets = {{
                .attachment = swapchainTexture,
                .load_op = rhi::ATTACHMENT_LOAD_OP_LOAD,
                .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
            }},
        });

        mImguiRenderer.render(cbEnd, u32(mFrameIdx));

        rhi::cmd_end_rendering(cbEnd);
    }

    rhi::cmd_texture_barrier(
        cbEnd,
        {{
            swapchainTexture,
            rhi::TEXTURE_LAYOUT_GENERAL,
            rhi::TEXTURE_LAYOUT_PRESENT_SRC,
            rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            rhi::ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            rhi::STAGE_NONE,
            rhi::ACCESS_NONE,
        }}
    );

    rhi::end_command_buffer(cbEnd);

    frame.uniformBufferManager.flush();
    rhi::queue_submit(
        rhi::QUEUE_GRAPHICS,
        {{
            .cb = cbEnd,
            .wait_semaphores =
                {
                    {frame.ssaoSemaphore.semaphore, frame.ssaoSemaphore.value},
                    {frame.shadowSemaphore.semaphore, frame.shadowSemaphore.value},
                },
            .signal_ready_to_present = true,
            .signal_semaphores = {
                {mFrameSemaphore, mUniformData.frameCount + 1},
            },
        }}
    );
}

void Renderer::CreateSwapchain(Vec2U32 size)
{
    rhi::device_wait_idle();

    CleanupSwapchain();
    CleanupColorResources();
    CleanupDepthResources();

    rhi::create_swapchain(size);

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

    CreateColorResources();
    CreateDepthResources();

    mSwapchainRecreated = true;
}

void Renderer::CleanupSwapchain()
{
    rhi::destroy_swapchain();
}

void Renderer::CreateColorResources()
{
    const Vec2U32 swapchainSize = rhi::get_texture_dimensions(rhi::get_swapchain_texture(0)).xy();
    const Vec3U32 renderDimensions
        = {swapchainSize.x * RENDER_SCALE, swapchainSize.y * RENDER_SCALE, 1};

    mRenderTexture = rhi::create_texture({
        .format = rhi::FORMAT_B10G11R11_UFLOAT_PACK32,
        .dimensions = renderDimensions,
        .usage = rhi::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | rhi::TEXTURE_USAGE_STORAGE_BIT
            | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "RenderTexture",
    });

    mUniformData.renderWidth = renderDimensions.x;
    mUniformData.renderHeight = renderDimensions.y;
    mUniformData.aspect = f32(mUniformData.renderWidth) / f32(mUniformData.renderHeight);
    mUniformData.renderTextureSizeInv
        = {1.0f / f32(renderDimensions.x), 1.0f / f32(renderDimensions.y)};

    mVisibilityTexture = rhi::create_texture({
        .format = rhi::FORMAT_R32G32_UINT,
        .dimensions = renderDimensions,
        .usage = rhi::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "VisibilityTexture",
    });

    const Vec3U32 aoDimensions = {renderDimensions.x / 2, renderDimensions.y / 2, 1};

    mSsaoPassData.width = aoDimensions.x;
    mSsaoPassData.height = aoDimensions.y;
    mSsaoPassData.textureSizeInv = {1.0f / f32(aoDimensions.x), 1.0f / f32(aoDimensions.y)};

    mAmbientOcclusionTexture = rhi::create_texture({
        .format = rhi::FORMAT_R8_UNORM,
        .dimensions = aoDimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "AmbientOcclusionTexture",
    });

    mAmbientOcclusionBlurredHorizontalTexture = rhi::create_texture({
        .format = rhi::get_texture_format(mAmbientOcclusionTexture),
        .dimensions = aoDimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "AmbientOcclusionBlurredHorizontalTexture",
    });

    mAmbientOcclusionBlurredVerticalTexture = rhi::create_texture({
        .format = rhi::get_texture_format(mAmbientOcclusionTexture),
        .dimensions = aoDimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "AmbientOcclusionBlurredVerticalTexture",
    });

    mAmbientOcclusionUpsampledTexture = rhi::create_texture({
        .format = rhi::get_texture_format(mAmbientOcclusionTexture),
        .dimensions = renderDimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "AmbientOcclusionBlurredUpsampledTexture",
    });

    mFogTexture = rhi::create_texture({
        .format = rhi::FORMAT_R16_SFLOAT,
        .dimensions = renderDimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "FogTexture",
    });

    mFogBlurredHorizontalTexture = rhi::create_texture({
        .format = rhi::get_texture_format(mFogTexture),
        .dimensions = renderDimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "FogBlurredHorizontalTexture",
    });

    mFogBlurredVerticalTexture = rhi::create_texture({
        .format = rhi::get_texture_format(mFogTexture),
        .dimensions = renderDimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "FogBlurredVerticalTexture",
    });

    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i)
    {
        mFrame[i].resolvedRenderTexture = rhi::create_texture({
            .format = rhi::get_texture_format(mRenderTexture),
            .dimensions = renderDimensions,
            .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT
                | rhi::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT,
            .debug_name = "ResolvedRenderTexture",
        });
    }

    mVelocityTexture = rhi::create_texture({
        .format = rhi::FORMAT_R16G16_SFLOAT,
        .dimensions = renderDimensions,
        .usage = rhi::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | rhi::TEXTURE_USAGE_STORAGE_BIT
            | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "VelocityTexture",
    });
}

void Renderer::CleanupColorResources()
{
    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i)
    {
        rhi::destroy_texture(mFrame[i].resolvedRenderTexture);
    }
    rhi::destroy_texture(mFogBlurredVerticalTexture);
    rhi::destroy_texture(mFogBlurredHorizontalTexture);
    rhi::destroy_texture(mFogTexture);
    rhi::destroy_texture(mAmbientOcclusionUpsampledTexture);
    rhi::destroy_texture(mAmbientOcclusionBlurredVerticalTexture);
    rhi::destroy_texture(mAmbientOcclusionBlurredHorizontalTexture);
    rhi::destroy_texture(mAmbientOcclusionTexture);
    rhi::destroy_texture(mVisibilityTexture);
    rhi::destroy_texture(mRenderTexture);
    rhi::destroy_texture(mVelocityTexture);
}

void Renderer::CreateDepthResources()
{
    const Vec3U32 renderDimensions = rhi::get_texture_dimensions(mRenderTexture);

    mDepthTexture = rhi::create_texture({
        .format = rhi::FORMAT_D32_SFLOAT,
        .dimensions = renderDimensions,
        .usage = rhi::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "DepthTexture",
    });

    mDepthViewQuarterResTexture = rhi::create_texture({
        .format = rhi::FORMAT_R16_SFLOAT,
        .dimensions = {renderDimensions.x / 2, renderDimensions.y / 2, 1},
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "DepthViewQuarterResTexture",
    });

    const Vec2U32 depthPyramidSize
        = {PreviousPow2(renderDimensions.x), PreviousPow2(renderDimensions.y)};
    const u32 depthPyramidMipLevels = utils::get_mip_level(depthPyramidSize.x, depthPyramidSize.y);

    // PreviousPow2 to make reductions at most by 2x2, otherwise they are not conservative.
    mDepthPyramidTexture = rhi::create_texture({
        .format = rhi::FORMAT_R32_SFLOAT,
        .dimensions = {depthPyramidSize.x, depthPyramidSize.y, 1},
        .mip_count = depthPyramidMipLevels,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "DepthPyramidTexture",
    });

    mCullPassData.depthPyramidWidth = f32(depthPyramidSize.x);
    mCullPassData.depthPyramidHeight = f32(depthPyramidSize.y);

    mDepthPyramidMipTextureDescriptors.resize(depthPyramidMipLevels);

    for (size_t i = 0; i < mDepthPyramidMipTextureDescriptors.size(); ++i)
    {
        mDepthPyramidMipTextureDescriptors[i] = rhi::create_texture_descriptor({
            .texture = mDepthPyramidTexture,
            .type = rhi::TEXTURE_TYPE_2D,
            .base_mip = u32(i),
            .mip_count = 1,
            .layer_count = 1,
        });
    }
}

void Renderer::CleanupDepthResources()
{
    rhi::destroy_texture(mDepthTexture);
    rhi::destroy_texture(mDepthViewQuarterResTexture);
    rhi::destroy_texture(mDepthPyramidTexture);
    for (rhi::TextureDescriptor h : mDepthPyramidMipTextureDescriptors)
    {
        rhi::destroy_texture_descriptor(h);
    }
}
