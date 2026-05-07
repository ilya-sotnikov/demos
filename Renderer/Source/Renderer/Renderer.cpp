#include "Renderer.hpp"

#include "../Utils.hpp"
#include "../Math/Vec2.hpp"
#include "../Math/Mat4.hpp"

#include <stdio.h>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cgltf.h>
#include <ktx.h>
#include <ktxvulkan.h>

#define SDL_PRINT_ERROR(functionName) \
    fprintf(stderr, "%s:%d: " functionName " failed: %s\n", __FILE__, __LINE__, SDL_GetError())

static u32 GetDispatchSize(u32 size, u32 workgroupSize)
{
    DEBUG_ASSERT(size > 0);
    DEBUG_ASSERT(workgroupSize % 2 == 0);

    return (size + workgroupSize - 1) / workgroupSize;
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

    if (!mDevice.Create(mSurface, mWindow))
    {
        return false;
    }

    if (!CreateSwapchain())
    {
        return false;
    }

    // Buffers.
    {
        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            // NOTE: creating a host visible, coherent, device local buffer.
            // Should be always legal even on discrete GPUs if total allocated
            // size is less than 200 MB or so. But I don't care about the size,
            // since resizable BAR is somewhat widely supported.
            if (!mDevice.CreateBuffer({
                    .buffer = mFrame[i].uniformBuffer,
                    .size = sizeof(UniformData),
                    .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                        | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    .debugName = "UniformBuffer",
                }))
            {
                return false;
            }
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDrawCountBuffer,
                .size = sizeof(u32),
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .debugName = "DrawCountBuffer",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mMeshPrimitiveVisibleBuffer,
                .size = sizeof(u32) * MAX_DRAW_CALLS,
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .debugName = "MeshPrimitiveVisibleBuffer",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDebugDrawCountBuffer,
                .size = sizeof(u32) * 1, // TODO: maybe enum max count for offsets?
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .debugName = "DebugDrawCountBuffer",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDebugDrawRectBuffer,
                .size = sizeof(DebugDrawRectData) * RENDERER_DEBUG_DRAW_RECT_MAX_COUNT,
                .debugName = "DebugDrawRectBuffer",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDebugDrawCmdBuffer,
                .size = sizeof(VkDrawIndirectCommand),
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                .debugName = "DebugDrawCmdBuffer",
            }))
        {
            return false;
        }
    }

    // Texture descriptor set layout.
    {
        const VkDescriptorSetLayoutBinding layoutBinding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = MAX_DESCRIPTOR_COUNT,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        };

        const VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
            | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

        const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = 1,
            .pBindingFlags = &bindingFlags,
        };

        const VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &bindingFlagsInfo,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = 1,
            .pBindings = &layoutBinding,
        };

        VK_CHECK(
            vkCreateDescriptorSetLayout(
                mDevice.mDevice,
                &layoutInfo,
                nullptr,
                &mTextureDescriptorSetLayout
            )

        );
    }

    if (!mDevice.CreateImage({
            .image = mShadowImage,
            .formats = {VK_FORMAT_D16_UNORM},
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = RENDERER_SHADOW_MAP_DIMENSIONS,
            .height = RENDERER_SHADOW_MAP_DIMENSIONS,
            .arrayLayers = RENDERER_SHADOW_MAP_CASCADE_COUNT,
            .debugName = "ShadowImage",
        }))
    {
        return false;
    }

    if (!RecompilePipelines())
    {
        return false;
    }

    // Command pools.
    {
        VkCommandPoolCreateInfo cmdPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = mDevice.mGraphicsQueueInfo.familyIdx,
        };
        VK_CHECK(
            vkCreateCommandPool(mDevice.mDevice, &cmdPoolInfo, nullptr, &mCommandPoolGraphics)
        );

        cmdPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = mDevice.mComputeQueueInfo.familyIdx,
        };
        VK_CHECK(vkCreateCommandPool(mDevice.mDevice, &cmdPoolInfo, nullptr, &mCommandPoolCompute));
    }

    // Command buffers.
    {
        const VkCommandBufferAllocateInfo cmdBufferGraphicsAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = mCommandPoolGraphics,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        const VkCommandBufferAllocateInfo cmdBufferComputeAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = mCommandPoolCompute,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VK_CHECK(vkAllocateCommandBuffers(
                mDevice.mDevice,
                &cmdBufferGraphicsAllocateInfo,
                &mFrame[i].commandBufferStart
            ));
            VK_CHECK(vkAllocateCommandBuffers(
                mDevice.mDevice,
                &cmdBufferGraphicsAllocateInfo,
                &mFrame[i].commandBufferShadow
            ));
            VK_CHECK(vkAllocateCommandBuffers(
                mDevice.mDevice,
                &cmdBufferGraphicsAllocateInfo,
                &mFrame[i].commandBufferEnd
            ));
            VK_CHECK(vkAllocateCommandBuffers(
                mDevice.mDevice,
                &cmdBufferComputeAllocateInfo,
                &mFrame[i].commandBufferSSAO
            ));
        }
    }

    // Synchronization primitives.
    {
        const VkSemaphoreCreateInfo semaphoreInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        mRenderFinishedSemaphores.resize(mSwapchain.images.size());
        for (VkSemaphore& sem : mRenderFinishedSemaphores)
        {
            VkSemaphore semaphore{};
            VK_CHECK(vkCreateSemaphore(mDevice.mDevice, &semaphoreInfo, nullptr, &semaphore));
            sem = semaphore;
        }

        const VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VK_CHECK(
                vkCreateFence(mDevice.mDevice, &fenceInfo, nullptr, &mFrame[i].fenceQueueSubmit)
            );
            VK_CHECK(vkCreateSemaphore(
                mDevice.mDevice,
                &semaphoreInfo,
                nullptr,
                &mFrame[i].semaphoreImageAcquire
            ));

            const VkSemaphoreTypeCreateInfo timelineSemaphoreTypeInfo = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            };
            const VkSemaphoreCreateInfo timelineSemaphoreInfo = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = &timelineSemaphoreTypeInfo,
            };

            VK_CHECK(vkCreateSemaphore(
                mDevice.mDevice,
                &timelineSemaphoreInfo,
                nullptr,
                &mFrame[i].semaphoreStart.semaphore
            ));
            VK_CHECK(vkCreateSemaphore(
                mDevice.mDevice,
                &timelineSemaphoreInfo,
                nullptr,
                &mFrame[i].semaphoreShadow.semaphore
            ));
            VK_CHECK(vkCreateSemaphore(
                mDevice.mDevice,
                &timelineSemaphoreInfo,
                nullptr,
                &mFrame[i].semaphoreSSAO.semaphore
            ));
        }
    }

    // Shadow map resources.
    {
        // One image view per cascade.
        for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
        {
            const VkImageViewCreateInfo imageViewInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = mShadowImage.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                .format = mShadowImage.format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .levelCount = 1,
                    .baseArrayLayer = u32(i),
                    .layerCount = 1,
                },
            };
            VK_CHECK(vkCreateImageView(
                mDevice.mDevice,
                &imageViewInfo,
                nullptr,
                &mShadowImageViewCascade[i]
            ));
        }

        {
            const VkSamplerCreateInfo samplerInfo = {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .compareEnable = VK_TRUE,
                .compareOp = VK_COMPARE_OP_GREATER,
                .maxLod = VK_LOD_CLAMP_NONE,
            };
            VK_CHECK(vkCreateSampler(mDevice.mDevice, &samplerInfo, nullptr, &mShadowSampler));
        }

        // PCF jitter offsets.
        {
            if (!mDevice.CreateImage({
                    .image = mShadowPcfJitterImage,
                    .formats = {VK_FORMAT_R8G8B8A8_SNORM},
                    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    .width = RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
                    .height = RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
                    .depth = RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U
                        * RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V / 2,
                    .debugName = "ShadowPcfJitterImage",
                }))
            {
                return false;
            }

            const std::vector<i8> jitterOffsets = CreateShadowJitterOffsets(
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U,
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V
            );

            Vulkan::Buffer stagingBuffer{};
            const VkDeviceSize uploadSize = VEC_SIZE_BYTES(jitterOffsets);
            if (!mDevice.CreateBuffer({
                    .buffer = stagingBuffer,
                    .size = uploadSize,
                    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    .requiredFlags
                    = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    .debugName = "StagingBuffer",
                }))
            {
                return false;
            }
            DEFER(mDevice.DestroyBuffer(stagingBuffer));

            memcpy(stagingBuffer.mapped, jitterOffsets.data(), uploadSize);

            const VkCommandBuffer cb = mFrame[0].commandBufferStart;

            VK_CHECK(vkResetCommandBuffer(cb, 0));

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_CHECK(vkBeginCommandBuffer(cb, &beginInfo));

            Vulkan::CmdImageMemoryBarrier(
                cb,
                {
                    Vulkan::ImageMemoryBarrier(
                        mShadowPcfJitterImage.image,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_2_HOST_BIT,
                        VK_ACCESS_2_NONE,
                        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT
                    ),
                }
            );

            VkImageSubresourceLayers imageSubresource{};
            imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageSubresource.layerCount = 1;

            VkBufferImageCopy bufferCopyRegion{};
            bufferCopyRegion.imageSubresource = imageSubresource;
            // clang-format off
            bufferCopyRegion.imageExtent = {
                u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE),
                u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE),
                u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U *
                    RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V / 2)
            };
            // clang-format on
            vkCmdCopyBufferToImage(
                cb,
                stagingBuffer.buffer,
                mShadowPcfJitterImage.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &bufferCopyRegion
            );

            Vulkan::CmdImageMemoryBarrier(
                cb,
                {
                    Vulkan::ImageMemoryBarrier(
                        mShadowPcfJitterImage.image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT
                    ),
                }
            );

            VK_CHECK(vkEndCommandBuffer(cb));

            if (!mDevice.QueueSubmit({
                    .queueInfo = mDevice.mGraphicsQueueInfo,
                    .commandBuffer = cb,
                }))
            {
                return false;
            }

            (void)mDevice.DeviceWaitIdle();

            const VkSamplerCreateInfo samplerInfo = {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_NEAREST,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            };
            VK_CHECK(
                vkCreateSampler(mDevice.mDevice, &samplerInfo, nullptr, &mShadowPcfJitterSampler)
            );
        }
    }

    // Samplers.
    {
        VkSamplerCreateInfo samplerInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .anisotropyEnable = VK_TRUE,
            .maxAnisotropy = 4.0f,
            .maxLod = 16.0f,
        };
        VK_CHECK(vkCreateSampler(mDevice.mDevice, &samplerInfo, nullptr, &mTextureSampler));

        samplerInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        };
        VK_CHECK(vkCreateSampler(mDevice.mDevice, &samplerInfo, nullptr, &mLinearSampler));

        samplerInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        };
        VK_CHECK(vkCreateSampler(mDevice.mDevice, &samplerInfo, nullptr, &mNearestSampler));

        const VkSamplerReductionModeCreateInfo reductionModeInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
            .reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN,
        };

        samplerInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = &reductionModeInfo,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = 16.0f,
        };
        VK_CHECK(vkCreateSampler(mDevice.mDevice, &samplerInfo, nullptr, &mMinSampler));
    }

    // Scene.
    {
        const std::string gltfPath = "../Assets/main_sponza/NewSponza_Main_glTF_003.gltf";

        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        std::vector<MeshPrimitive> meshPrimitives;
        std::vector<VkDrawIndexedIndirectCommand> drawCmds;
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

        if (!mDevice.CreateBuffer({
                .buffer = mVertexBuffer,
                .size = VEC_SIZE_BYTES(vertices),
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                    | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                .debugName = "VertexBuffer",
            }))
        {
            return false;
        }
        memcpy(mVertexBuffer.mapped, vertices.data(), VEC_SIZE_BYTES(vertices));
        mDevice.UnmapBuffer(mVertexBuffer);

        if (!mDevice.CreateBuffer({
                .buffer = mIndexBuffer,
                .size = VEC_SIZE_BYTES(indices),
                .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                    | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                .debugName = "IndexBuffer",
            }))
        {
            return false;
        }
        memcpy(mIndexBuffer.mapped, indices.data(), VEC_SIZE_BYTES(indices));
        mDevice.UnmapBuffer(mIndexBuffer);

        if (!mDevice.CreateBuffer({
                .buffer = mDrawCmdBuffer1,
                .size = sizeof(VkDrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                    | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                .debugName = "DrawCmdBuffer1",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDrawCmdEarlyBuffer2,
                .size = sizeof(VkDrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                .debugName = "DrawCmdEarlyBuffer2",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDrawCmdLateBuffer2,
                .size = sizeof(VkDrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                .debugName = "DrawCmdLateBuffer2",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDrawCmdShadowBuffer,
                .size = sizeof(VkDrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                .debugName = "DrawCmdShadowBuffer",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDrawIndicesEarlyBuffer,
                .size = sizeof(u32) * MAX_DRAW_CALLS,
                .debugName = "DrawIndicesEarlyBuffer",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDrawIndicesLateBuffer,
                .size = sizeof(u32) * MAX_DRAW_CALLS,
                .debugName = "DrawIndicesLateBuffer",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDrawIndicesShadowBuffer,
                .size = sizeof(u32) * MAX_DRAW_CALLS,
                .debugName = "DrawIndicesShadowBuffer",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mMaterialBuffer,
                .size = sizeof(Material) * MAX_DRAW_CALLS,
                .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                    | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                .debugName = "MaterialBuffer",
            }))
        {
            return false;
        }

        if (!mDevice.CreateBuffer({
                .buffer = mDrawDataBuffer,
                .size = sizeof(DrawData) * MAX_DRAW_CALLS,
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                    | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                .debugName = "DrawDataBuffer",
            }))
        {
            return false;
        }

        memcpy(mDrawCmdBuffer1.mapped, drawCmds.data(), VEC_SIZE_BYTES(drawCmds));

        memcpy(mMaterialBuffer.mapped, materials.data(), VEC_SIZE_BYTES(materials));

        memcpy(mDrawDataBuffer.mapped, drawData.data(), VEC_SIZE_BYTES(drawData));
    }

    // Descriptor pool, descriptor set.
    {
        const VkDescriptorPoolSize poolSizes[] = {
            {
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                MAX_DESCRIPTOR_COUNT,
            },
        };

        const VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets = 1,
            .poolSizeCount = ARRAY_SIZE(poolSizes),
            .pPoolSizes = poolSizes,
        };
        VK_CHECK(vkCreateDescriptorPool(mDevice.mDevice, &poolInfo, nullptr, &mDescriptorPool));

        const VkDescriptorSetAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = mDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &mTextureDescriptorSetLayout,
        };

        VK_CHECK(vkAllocateDescriptorSets(mDevice.mDevice, &allocateInfo, &mTextureDescriptorSet));

        for (size_t i = 0; i < mTextures.size(); ++i)
        {
            const VkDescriptorImageInfo imageInfo = {
                .imageView = mTextures[i].view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            const VkWriteDescriptorSet writeSet = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = mTextureDescriptorSet,
                .dstBinding = 0,
                .dstArrayElement = u32(i),
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = &imageInfo,
            };

            vkUpdateDescriptorSets(mDevice.mDevice, 1, &writeSet, 0, nullptr);
        }
    }

    if (!mImguiRenderer
             .Init(mWindow, mDevice, mCommandPoolGraphics, mSwapchain.surfaceFormat.format))
    {
        fprintf(stderr, "Failed to initialize ImGui renderer\n");
        return false;
    }

    // Initializing resources.
    {
        const VkCommandBuffer cb = mFrame[0].commandBufferStart;

        VK_CHECK(vkResetCommandBuffer(cb, 0));

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        VK_CHECK(vkBeginCommandBuffer(cb, &beginInfo));

        vkCmdFillBuffer(cb, mMeshPrimitiveVisibleBuffer.buffer, 0, sizeof(u32) * MAX_DRAW_CALLS, 0);

        VK_CHECK(vkEndCommandBuffer(cb));

        if (!mDevice.QueueSubmit({
                .queueInfo = mDevice.mGraphicsQueueInfo,
                .commandBuffer = cb,
            }))
        {
            return false;
        }
        if (!mDevice.QueueWaitIdle(mDevice.mGraphicsQueueInfo))
        {
            return false;
        }
    }

    mSwapchainNeedsRecreating = true;
    mTaaJitterMaxIdx = 8;
    mUniformData.taaBlendWeight = 0.1f;
    mUniformData.ambientIntensity = 0.04f;
    mUniformData.sunIntensity = 1.0f;
    mUniformData.gradErrorMax = 0.01f;

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
    if (!mDevice.mDevice)
    {
        return;
    }

    (void)mDevice.DeviceWaitIdle();

    mImguiRenderer.Cleanup();

    CleanupPipelines();
    CleanupColorResources();
    CleanupDepthResources();

    for (Vulkan::Image& tex : mTextures)
    {
        mDevice.DestroyImage(tex);
    }

    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        vkDestroyImageView(mDevice.mDevice, mShadowImageViewCascade[i], nullptr);
    }
    mDevice.DestroyImage(mShadowImage);
    mDevice.DestroyImage(mShadowPcfJitterImage);
    mDevice.DestroyBuffer(mDebugDrawCmdBuffer);
    mDevice.DestroyBuffer(mDebugDrawRectBuffer);
    mDevice.DestroyBuffer(mDebugDrawCountBuffer);
    mDevice.DestroyBuffer(mMeshPrimitiveVisibleBuffer);
    mDevice.DestroyBuffer(mDrawCountBuffer);
    mDevice.DestroyBuffer(mMaterialBuffer);
    mDevice.DestroyBuffer(mDrawDataBuffer);
    mDevice.DestroyBuffer(mVertexBuffer);
    mDevice.DestroyBuffer(mIndexBuffer);
    mDevice.DestroyBuffer(mDrawIndicesShadowBuffer);
    mDevice.DestroyBuffer(mDrawIndicesEarlyBuffer);
    mDevice.DestroyBuffer(mDrawIndicesLateBuffer);
    mDevice.DestroyBuffer(mDrawCmdShadowBuffer);
    mDevice.DestroyBuffer(mDrawCmdEarlyBuffer2);
    mDevice.DestroyBuffer(mDrawCmdLateBuffer2);
    mDevice.DestroyBuffer(mDrawCmdBuffer1);
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        mDevice.DestroyBuffer(mFrame[i].uniformBuffer);
    }
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroyFence(mDevice.mDevice, mFrame[i].fenceQueueSubmit, nullptr);
        vkDestroySemaphore(mDevice.mDevice, mFrame[i].semaphoreImageAcquire, nullptr);
        vkDestroySemaphore(mDevice.mDevice, mFrame[i].semaphoreStart.semaphore, nullptr);
        vkDestroySemaphore(mDevice.mDevice, mFrame[i].semaphoreShadow.semaphore, nullptr);
        vkDestroySemaphore(mDevice.mDevice, mFrame[i].semaphoreSSAO.semaphore, nullptr);
    }
    for (VkSemaphore sem : mRenderFinishedSemaphores)
    {
        vkDestroySemaphore(mDevice.mDevice, sem, nullptr);
    }
    vkDestroyDescriptorPool(mDevice.mDevice, mDescriptorPool, nullptr);
    vkDestroySampler(mDevice.mDevice, mShadowPcfJitterSampler, nullptr);
    vkDestroySampler(mDevice.mDevice, mShadowSampler, nullptr);
    vkDestroySampler(mDevice.mDevice, mMinSampler, nullptr);
    vkDestroySampler(mDevice.mDevice, mNearestSampler, nullptr);
    vkDestroySampler(mDevice.mDevice, mLinearSampler, nullptr);
    vkDestroySampler(mDevice.mDevice, mTextureSampler, nullptr);
    vkDestroyCommandPool(mDevice.mDevice, mCommandPoolCompute, nullptr);
    vkDestroyCommandPool(mDevice.mDevice, mCommandPoolGraphics, nullptr);
    vkDestroyDescriptorSetLayout(mDevice.mDevice, mTextureDescriptorSetLayout, nullptr);
    CleanupSwapchain();
    vkDestroySurfaceKHR(mDevice.mInstance, mSurface, nullptr);
    mDevice.Destroy();
    volkFinalize();
}

bool Renderer::StartNewFrame()
{
    DEBUG_ASSERT(!mNewFrameStarted);

    Frame& frame = mFrame[mFrameIdx];

    VK_CHECK(vkWaitForFences(mDevice.mDevice, 1, &frame.fenceQueueSubmit, VK_TRUE, 1'000'000'000));
    VK_CHECK(vkResetFences(mDevice.mDevice, 1, &frame.fenceQueueSubmit));

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

    if (mSwapchainNeedsRecreating)
    {
        mSwapchainNeedsRecreating = false;

        if (!CreateSwapchain())
        {
            return false;
        }
    }

    Frame& frame = mFrame[mFrameIdx];

    // I can't be arsed to handle the edge cases and it's only used for debugging.
    if (mRenderModeChanged)
    {
        (void)mDevice.DeviceWaitIdle();

        const VkCommandBuffer cb = frame.commandBufferStart;

        VK_CHECK(vkResetCommandBuffer(cb, 0));

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };

        VK_CHECK(vkBeginCommandBuffer(cb, &beginInfo));

        Vulkan::CmdMemoryBarrier(
            cb,
            {
                Vulkan::MemoryBarrier(
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT
                ),
            }
        );

        VK_CHECK(vkEndCommandBuffer(cb));

        if (!mDevice.QueueSubmit({
                .queueInfo = mDevice.mGraphicsQueueInfo,
                .commandBuffer = frame.commandBufferStart,
            }))
        {
            return false;
        }

        (void)mDevice.DeviceWaitIdle();
    }

    u32 imageIdx = 0;
    VkResult vulkanResult = vkAcquireNextImageKHR(
        mDevice.mDevice,
        mSwapchain.swapchain,
        1'000'000'000,
        frame.semaphoreImageAcquire,
        nullptr,
        &imageIdx
    );
    if (vulkanResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        mSwapchainNeedsRecreating = true;
        return true;
    }
    else if (vulkanResult != VK_SUCCESS && vulkanResult != VK_SUBOPTIMAL_KHR)
    {
        VK_CHECK_PRINT_ERROR(vulkanResult);
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

    memcpy(frame.uniformBuffer.mapped, &mUniformData, sizeof(mUniformData));

    switch (mUniformData.renderMode)
    {
    case RENDER_MODE_FORWARD:
        if (!RecordCommandBufferForward(imageIdx))
        {
            return false;
        }
        break;
    case RENDER_MODE_GRAD_ERROR:
        if (!RecordCommandBufferDebugGradError(imageIdx))
        {
            return false;
        }
        break;
    default:
        if (!RecordCommandBufferVisibility(imageIdx))
        {
            return false;
        }
        break;
    }

    if (mUniformData.renderMode == RENDER_MODE_FORWARD
        || mUniformData.renderMode == RENDER_MODE_GRAD_ERROR)
    {
        if (!mDevice.QueueSubmit({
                .queueInfo = mDevice.mGraphicsQueueInfo,
                .waitSemaphores = {frame.semaphoreImageAcquire},
                .waitDstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .commandBuffer = frame.commandBufferStart,
                .signalSemaphores = {mRenderFinishedSemaphores[imageIdx]},
                .fence = frame.fenceQueueSubmit,
            }))
        {
            return false;
        }
    }
    else
    {
        // TODO: wrapper or something.
        const VkSemaphoreSubmitInfo semWaitSubmitInfoStart = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = frame.semaphoreImageAcquire,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
        const VkCommandBufferSubmitInfo cbSubmitInfoStart = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = frame.commandBufferStart,
        };
        const VkSemaphoreSubmitInfo semSignalSubmitInfoStart = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = frame.semaphoreStart.semaphore,
            .value = frame.semaphoreStart.Inc(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };

        const VkSemaphoreSubmitInfo semWaitSubmitInfoSSAO = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = frame.semaphoreStart.semaphore,
            .value = semSignalSubmitInfoStart.value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };
        const VkCommandBufferSubmitInfo cbSubmitInfoSSAO = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = frame.commandBufferSSAO,
        };
        const VkSemaphoreSubmitInfo semSignalSubmitInfoSSAO = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = frame.semaphoreSSAO.semaphore,
            .value = frame.semaphoreSSAO.Inc(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };

        const VkSemaphoreSubmitInfo semWaitSubmitInfoShadow = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = frame.semaphoreStart.semaphore,
            .value = semSignalSubmitInfoStart.value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };
        const VkCommandBufferSubmitInfo cbSubmitInfoShadow = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = frame.commandBufferShadow,
        };
        const VkSemaphoreSubmitInfo semSignalSubmitInfoShadow = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = frame.semaphoreShadow.semaphore,
            .value = frame.semaphoreShadow.Inc(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };

        const VkSemaphoreSubmitInfo semWaitSubmitInfosEnd[] = {
            {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = frame.semaphoreSSAO.semaphore,
                .value = semSignalSubmitInfoSSAO.value,
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            },
            {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = frame.semaphoreShadow.semaphore,
                .value = semSignalSubmitInfoShadow.value,
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            },
        };
        const VkCommandBufferSubmitInfo cbSubmitInfoEnd = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = frame.commandBufferEnd,
        };
        const VkSemaphoreSubmitInfo semSignalSubmitInfoEnd = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = mRenderFinishedSemaphores[imageIdx],
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };

        const VkSubmitInfo2 submitInfosStart[] = {
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .waitSemaphoreInfoCount = 1,
                .pWaitSemaphoreInfos = &semWaitSubmitInfoStart,
                .commandBufferInfoCount = 1,
                .pCommandBufferInfos = &cbSubmitInfoStart,
                .signalSemaphoreInfoCount = 1,
                .pSignalSemaphoreInfos = &semSignalSubmitInfoStart,
            },
        };

        VK_CHECK(vkQueueSubmit2(
            mDevice.mGraphicsQueueInfo.queue,
            ARRAY_SIZE(submitInfosStart),
            submitInfosStart,
            VK_NULL_HANDLE
        ));

        const VkSubmitInfo2 submitInfosSSAO[] = {
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .waitSemaphoreInfoCount = 1,
                .pWaitSemaphoreInfos = &semWaitSubmitInfoSSAO,
                .commandBufferInfoCount = 1,
                .pCommandBufferInfos = &cbSubmitInfoSSAO,
                .signalSemaphoreInfoCount = 1,
                .pSignalSemaphoreInfos = &semSignalSubmitInfoSSAO,
            },
        };

        VK_CHECK(vkQueueSubmit2(
            mDevice.mComputeQueueInfo.queue,
            ARRAY_SIZE(submitInfosSSAO),
            submitInfosSSAO,
            VK_NULL_HANDLE
        ));

        const VkSubmitInfo2 submitInfosShadow[] = {
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .waitSemaphoreInfoCount = 1,
                .pWaitSemaphoreInfos = &semWaitSubmitInfoShadow,
                .commandBufferInfoCount = 1,
                .pCommandBufferInfos = &cbSubmitInfoShadow,
                .signalSemaphoreInfoCount = 1,
                .pSignalSemaphoreInfos = &semSignalSubmitInfoShadow,
            },
        };

        VK_CHECK(vkQueueSubmit2(
            mDevice.mGraphicsQueueInfo.queue,
            ARRAY_SIZE(submitInfosShadow),
            submitInfosShadow,
            VK_NULL_HANDLE
        ));

        const VkSubmitInfo2 submitInfosEnd[] = {
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .waitSemaphoreInfoCount = ARRAY_SIZE(semWaitSubmitInfosEnd),
                .pWaitSemaphoreInfos = semWaitSubmitInfosEnd,
                .commandBufferInfoCount = 1,
                .pCommandBufferInfos = &cbSubmitInfoEnd,
                .signalSemaphoreInfoCount = 1,
                .pSignalSemaphoreInfos = &semSignalSubmitInfoEnd,
            },
        };

        VK_CHECK(vkQueueSubmit2(
            mDevice.mGraphicsQueueInfo.queue,
            ARRAY_SIZE(submitInfosEnd),
            submitInfosEnd,
            frame.fenceQueueSubmit
        ));
    }

    vulkanResult = mDevice.QueuePresent({
        .queueInfo = mDevice.mGraphicsQueueInfo,
        .waitSemaphores = {mRenderFinishedSemaphores[imageIdx]},
        .swapchain = mSwapchain.swapchain,
        .imageIdx = imageIdx,
    });
    if (vulkanResult == VK_ERROR_OUT_OF_DATE_KHR || vulkanResult == VK_SUBOPTIMAL_KHR)
    {
        mSwapchainNeedsRecreating = true;
    }
    else if (vulkanResult != VK_SUCCESS)
    {
        VK_CHECK_PRINT_ERROR(vulkanResult);
        return false;
    }

    mNewFrameStarted = false;

    mPrevFrameIdx = mFrameIdx;
    mFrameIdx = (mFrameIdx + 1) % RENDERER_MAX_FRAMES_IN_FLIGHT;
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
    mDevice.DestroyPipeline(mShadowCullPipeline);
    mDevice.DestroyPipeline(mShadowPipeline);
    mDevice.DestroyPipeline(mAmbientOcclusionUpsamplePipeline);
    mDevice.DestroyPipeline(mAmbientOcclusionBlurPipeline);
    mDevice.DestroyPipeline(mAmbientOcclusionPipeline);
    mDevice.DestroyPipeline(mDepthViewQuarterResPipeline);
    mDevice.DestroyPipeline(mDebugDrawFillCmdPipeline);
    mDevice.DestroyPipeline(mDebugDrawRectPipeline);
    mDevice.DestroyPipeline(mDepthReducePipeline);
    mDevice.DestroyPipeline(mDebugGradErrorPipeline);
    mDevice.DestroyPipeline(mTaaResolvePipeline);
    mDevice.DestroyPipeline(mCullLatePipeline);
    mDevice.DestroyPipeline(mCullEarlyPipeline);
    mDevice.DestroyPipeline(mFullscreenPipeline);
    mDevice.DestroyPipeline(mVisibilityRenderPipeline);
    mDevice.DestroyPipeline(mForwardRenderPipeline);
    mDevice.DestroyPipeline(mVisibilityPipeline);
}

bool Renderer::RecompilePipelines()
{
    (void)mDevice.DeviceWaitIdle();

    CleanupPipelines();

    // Graphics pipelines
    if (!mDevice.CreateGraphicsPipeline({
            .pipeline = mVisibilityPipeline,
            .shaderPaths = {"VisibilityBuffer.vert.hlsl.spv", "VisibilityBuffer.frag.hlsl.spv"},
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .depthFormat = mDepthImage.format,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .colorAttachmentFormats = {mVisibilityImage.format},
            .colorBlendAttachments = {{.colorWriteMask = Vulkan::ColorComponentAllBits}},
            .dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
            .debugName = "VisibilityBufferPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateGraphicsPipeline({
            .pipeline = mForwardRenderPipeline,
            .shaderPaths = {"ForwardRender.vert.hlsl.spv", "ForwardRender.frag.hlsl.spv"},
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .depthFormat = mDepthImage.format,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .colorAttachmentFormats = {mRenderImage.format, mVelocityImage.format},
            .colorBlendAttachments = {
                {.colorWriteMask = Vulkan::ColorComponentAllBits},
                {.colorWriteMask = Vulkan::ColorComponentAllBits},
            },
            .dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
            .extraDescriptorSetLayout = mTextureDescriptorSetLayout,
            .debugName = "ForwardRenderPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateGraphicsPipeline({
            .pipeline = mDebugDrawRectPipeline,
            .shaderPaths = {"DebugDrawRect.vert.hlsl.spv", "DebugDrawRect.frag.hlsl.spv"},
            .colorAttachmentFormats = {mRenderImage.format},
            .colorBlendAttachments = {{.colorWriteMask = Vulkan::ColorComponentAllBits}},
            .dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
            .debugName = "DebugDrawRectPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateGraphicsPipeline({
            .pipeline = mFullscreenPipeline,
            .shaderPaths = {"Fullscreen.vert.hlsl.spv", "Fullscreen.frag.hlsl.spv"},
            .colorAttachmentFormats = {mSwapchain.surfaceFormat.format},
            .colorBlendAttachments = {{.colorWriteMask = Vulkan::ColorComponentAllBits}},
            .dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
            .debugName = "FullscreenPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateGraphicsPipeline({
            .pipeline = mDebugGradErrorPipeline,
            .shaderPaths = {"DebugGradError.vert.hlsl.spv", "DebugGradError.frag.hlsl.spv"},
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .depthFormat = mDepthImage.format,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .colorAttachmentFormats = {mSwapchain.surfaceFormat.format},
            .colorBlendAttachments = {{.colorWriteMask = Vulkan::ColorComponentAllBits}},
            .dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
            .debugName = "DebugGradErrorPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateGraphicsPipeline({
            .pipeline = mShadowPipeline,
            .shaderPaths = {"Shadow.vert.hlsl.spv", "Shadow.frag.hlsl.spv"},
            .depthClampEnable = VK_TRUE,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .depthFormat = mShadowImage.format,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
            .debugName = "ShadowPass",
        }))
    {
        return false;
    }

    // Compute pipelines.
    if (!mDevice.CreateComputePipeline({
            .pipeline = mCullEarlyPipeline,
            .shaderPath = "Cull.comp.hlsl.spv",
            .specializationConstants = {0},
            .debugName = "CullEarlyPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateComputePipeline({
            .pipeline = mCullLatePipeline,
            .shaderPath = "Cull.comp.hlsl.spv",
            .specializationConstants = {1},
            .debugName = "CullLatePass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateComputePipeline({
            .pipeline = mShadowCullPipeline,
            .shaderPath = "ShadowCull.comp.hlsl.spv",
            .debugName = "ShadowCullPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateComputePipeline({
            .pipeline = mVisibilityRenderPipeline,
            .shaderPath = "VisibilityRender.comp.hlsl.spv",
            .extraDescriptorSetLayout = mTextureDescriptorSetLayout,
            .debugName = "VisibilityRenderPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateComputePipeline({
            .pipeline = mTaaResolvePipeline,
            .shaderPath = "TaaResolve.comp.hlsl.spv",
            .debugName = "TaaResolvePass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateComputePipeline({
            .pipeline = mDepthReducePipeline,
            .shaderPath = "DepthReduce.comp.hlsl.spv",
            .debugName = "DepthReducePass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateComputePipeline({
            .pipeline = mDepthViewQuarterResPipeline,
            .shaderPath = "DepthViewQuarterRes.comp.hlsl.spv",
            .debugName = "DepthViewQuarterResPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateComputePipeline({
            .pipeline = mAmbientOcclusionPipeline,
            .shaderPath = "SSAO.comp.hlsl.spv",
            .debugName = "AmbientOcclusionPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateComputePipeline({
            .pipeline = mAmbientOcclusionBlurPipeline,
            .shaderPath = "BlurSSAO.comp.hlsl.spv",
            .debugName = "AmbientOcclusionBlurPass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateComputePipeline({
            .pipeline = mAmbientOcclusionUpsamplePipeline,
            .shaderPath = "UpsampleSSAO.comp.hlsl.spv",
            .debugName = "AmbientOcclusionUpsamplePass",
        }))
    {
        return false;
    }

    if (!mDevice.CreateComputePipeline({
            .pipeline = mDebugDrawFillCmdPipeline,
            .shaderPath = "DebugDrawFillCmd.comp.hlsl.spv",
            .debugName = "DebugDrawFillCmdPass",
        }))
    {
        return false;
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

        const VkFormat format = ktxTexture2_GetVkFormat(ktxTex);
        VkExtent3D extent{ktxTex->baseWidth, ktxTex->baseHeight, ktxTex->baseDepth};
        const u32 mipLevels = ktxTex->numLevels;
        const ktx_size_t size = ktxTexture_GetDataSize(ktxTexture(ktxTex));
        const ktx_uint8_t* ktxData = ktxTexture_GetData(ktxTexture(ktxTex));

        if (!mDevice.CreateImage({
                .image = mTextures[i],
                .formats = {format},
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .width = ktxTex->baseWidth,
                .height = ktxTex->baseHeight,
                .mipLevels = mipLevels,
                .debugName = texturePaths[i].c_str(),
            }))
        {
            return false;
        }

        Vulkan::Buffer stagingBuffer{};
        if (!mDevice.CreateBuffer({
                .buffer = stagingBuffer,
                .size = size,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .requiredFlags
                = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                .debugName = "StagingBuffer",
            }))
        {
            return false;
        }
        DEFER(mDevice.DestroyBuffer(stagingBuffer));

        std::vector<VkBufferImageCopy> copyRegions(mipLevels);

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
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = mipLevel,
                    .layerCount = 1,
                },
                .imageExtent = {
                    .width = extent.width >> mipLevel,
                    .height = extent.height >> mipLevel,
                    .depth = 1,
                }
            };
        }
        memcpy(stagingBuffer.mapped, ktxData, size);
        mDevice.UnmapBuffer(stagingBuffer);

        const VkCommandBuffer cb = mFrame[0].commandBufferStart;

        VK_CHECK(vkResetCommandBuffer(cb, 0));

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        VK_CHECK(vkBeginCommandBuffer(cb, &beginInfo));

        Vulkan::CmdImageMemoryBarrier(
            cb,
            {
                Vulkan::ImageMemoryBarrier(
                    mTextures[i].image,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE,
                    VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    mipLevels
                ),
            }
        );

        vkCmdCopyBufferToImage(
            cb,
            stagingBuffer.buffer,
            mTextures[i].image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            u32(copyRegions.size()),
            copyRegions.data()
        );

        Vulkan::CmdImageMemoryBarrier(
            cb,
            {
                Vulkan::ImageMemoryBarrier(
                    mTextures[i].image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_NONE,
                    VK_ACCESS_2_NONE,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    mipLevels
                ),
            }
        );

        VK_CHECK(vkEndCommandBuffer(cb));

        if (!mDevice.QueueSubmit({
                .queueInfo = mDevice.mGraphicsQueueInfo,
                .commandBuffer = cb,
            }))
        {
            return false;
        }
        if (!mDevice.QueueWaitIdle(mDevice.mGraphicsQueueInfo))
        {
            return false;
        }
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

void Renderer::VisibilityBufferPass(VkCommandBuffer cb, bool cullLate)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mVisibilityPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mVisibilityPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            cullLate ? mDrawIndicesLateBuffer.buffer : mDrawIndicesEarlyBuffer.buffer,
            mDrawDataBuffer.buffer,
            mVertexBuffer.buffer,
        }
    );

    const PushConstantsVisibilityBuffer pushConstants = {
        .cullLate = cullLate,
    };
    vkCmdPushConstants(
        cb,
        mVisibilityPipeline.layout,
        VK_SHADER_STAGE_ALL,
        0,
        sizeof(pushConstants),
        &pushConstants
    );

    const VkViewport viewport = {
        .y = f32(mRenderImageExtent.height),
        .width = f32(mRenderImageExtent.width),
        .height = -f32(mRenderImageExtent.height),
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cb, 0, 1, &viewport);

    const VkRect2D scissor = {
        .extent = mRenderImageExtent,
    };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    const VkRenderingAttachmentInfo renderingAttachmentInfos[] = {
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = mVisibilityImage.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = cullLate ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        },
    };

    const VkRenderingAttachmentInfo depthAttachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = mDepthImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = cullLate ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    const VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = mRenderImageExtent},
        .layerCount = 1,
        .colorAttachmentCount = ARRAY_SIZE(renderingAttachmentInfos),
        .pColorAttachments = renderingAttachmentInfos,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    vkCmdBeginRendering(cb, &renderingInfo);

    vkCmdBindIndexBuffer(cb, mIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirectCount(
        cb,
        cullLate ? mDrawCmdLateBuffer2.buffer : mDrawCmdEarlyBuffer2.buffer,
        0,
        mDrawCountBuffer.buffer,
        0,
        mUniformData.drawCount,
        sizeof(VkDrawIndexedIndirectCommand)
    );

    vkCmdEndRendering(cb);
}

void Renderer::ForwardPass(VkCommandBuffer cb, bool cullLate)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mForwardRenderPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mForwardRenderPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            cullLate ? mDrawIndicesLateBuffer.buffer : mDrawIndicesEarlyBuffer.buffer,
            mDrawDataBuffer.buffer,
            mVertexBuffer.buffer,
            mMaterialBuffer.buffer,
            mTextureSampler,
        }
    );

    vkCmdBindDescriptorSets(
        cb,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        mForwardRenderPipeline.layout,
        1,
        1,
        &mTextureDescriptorSet,
        0,
        nullptr
    );

    const VkViewport viewport = {
        .y = f32(mRenderImageExtent.height),
        .width = f32(mRenderImageExtent.width),
        .height = -f32(mRenderImageExtent.height),
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cb, 0, 1, &viewport);

    const VkRect2D scissor = {
        .extent = mRenderImageExtent,
    };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    const Vec3 clearColor = Utils::SrgbToLinear({0.7f, 0.8f, 0.9f});

    const VkRenderingAttachmentInfo renderingAttachmentInfos[] = {
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = mRenderImage.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = cullLate ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{clearColor.R(), clearColor.G(), clearColor.B()}}},
        },
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = mVelocityImage.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = cullLate ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        },
    };

    const VkRenderingAttachmentInfo depthAttachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = mDepthImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = cullLate ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    const VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = mRenderImageExtent},
        .layerCount = 1,
        .colorAttachmentCount = ARRAY_SIZE(renderingAttachmentInfos),
        .pColorAttachments = renderingAttachmentInfos,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    vkCmdBeginRendering(cb, &renderingInfo);

    vkCmdBindIndexBuffer(cb, mIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirectCount(
        cb,
        cullLate ? mDrawCmdLateBuffer2.buffer : mDrawCmdEarlyBuffer2.buffer,
        0,
        mDrawCountBuffer.buffer,
        0,
        mUniformData.drawCount,
        sizeof(VkDrawIndexedIndirectCommand)
    );

    vkCmdEndRendering(cb);
}

void Renderer::CullPass(VkCommandBuffer cb, bool late)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(
        cb,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        late ? mCullLatePipeline.pipeline : mCullEarlyPipeline.pipeline
    );

    Vulkan::CmdPushDescriptors(
        cb,
        late ? mCullLatePipeline : mCullEarlyPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mDrawDataBuffer.buffer,
            mDrawCountBuffer.buffer,
            mDrawCmdBuffer1.buffer,
            late ? mDrawCmdLateBuffer2.buffer : mDrawCmdEarlyBuffer2.buffer,
            late ? mDrawIndicesLateBuffer.buffer : mDrawIndicesEarlyBuffer.buffer,
            mMeshPrimitiveVisibleBuffer.buffer,
            mMinSampler,
            // TODO: this is so fucking hacky, early pass doesn't use depth pyramid,
            // early/late pipelines are created with different specialization constants,
            // but this binding exists in both and must be filled correctly, for now
            // just using a dummy image for the early pass.
            {
                late ? mDepthPyramidImage.view : mTextures[0].view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            mDebugDrawCountBuffer.buffer,
            mDebugDrawRectBuffer.buffer,
        }
    );

    vkCmdDispatch(cb, GetDispatchSize(mUniformData.drawCount, RENDERER_CULL_WORKGROUP_SIZE), 1, 1);
}

void Renderer::DepthReducePass(VkCommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mDepthReducePipeline.pipeline);

    PushConstantsDepthReduce pushConstants{};

    // In, out.
    Vulkan::DescriptorInfo descInfos[] = {
        {mDepthImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {mDepthPyramidMipImageViews[0], VK_IMAGE_LAYOUT_GENERAL},
        mMinSampler,
    };

    for (size_t i = 0; i < mDepthPyramidMipImageViews.size(); ++i)
    {
        descInfos[1].image.imageView = mDepthPyramidMipImageViews[i];

        const u32 width = Max(1U, mDepthPyramidImageExtent.width >> i);
        const u32 height = Max(1U, mDepthPyramidImageExtent.height >> i);

        pushConstants.outWidth = width;
        pushConstants.outHeight = height;
        pushConstants.mipLevel = u32(i);

        vkCmdPushConstants(
            cb,
            mDepthReducePipeline.layout,
            VK_SHADER_STAGE_ALL,
            0,
            sizeof(pushConstants),
            &pushConstants
        );

        vkCmdPushDescriptorSetWithTemplate(
            cb,
            mDepthReducePipeline.descriptorUpdateTemplate,
            mDepthReducePipeline.layout,
            0,
            descInfos
        );

        vkCmdDispatch(
            cb,
            GetDispatchSize(width, RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_X),
            GetDispatchSize(height, RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_Y),
            1
        );

        Vulkan::CmdMemoryBarrier(
            cb,
            {
                Vulkan::MemoryBarrier(
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                ),
            }
        );

        descInfos[0].image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        descInfos[0].image.imageView = mDepthPyramidMipImageViews[i];
    }
}

void Renderer::DepthViewQuarterResPass(VkCommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mDepthViewQuarterResPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mDepthViewQuarterResPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            {mDepthImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mDepthViewQuarterResImage.view, VK_IMAGE_LAYOUT_GENERAL},
        }
    );

    vkCmdDispatch(
        cb,
        GetDispatchSize(mAmbientOcclusionImageExtent.width, RENDERER_SSAO_WORKGROUP_SIZE_X),
        GetDispatchSize(mAmbientOcclusionImageExtent.height, RENDERER_SSAO_WORKGROUP_SIZE_Y),
        1
    );
}

void Renderer::AmbientOcclusionPass(VkCommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mAmbientOcclusionPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mAmbientOcclusionPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mDrawIndicesEarlyBuffer.buffer,
            mDrawIndicesLateBuffer.buffer,
            mDrawCmdEarlyBuffer2.buffer,
            mDrawCmdLateBuffer2.buffer,
            mDrawDataBuffer.buffer,
            mIndexBuffer.buffer,
            mVertexBuffer.buffer,
            mNearestSampler,
            {mDepthViewQuarterResImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mVisibilityImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mAmbientOcclusionImage.view, VK_IMAGE_LAYOUT_GENERAL},
        }
    );

    vkCmdDispatch(
        cb,
        GetDispatchSize(mAmbientOcclusionImageExtent.width, RENDERER_SSAO_WORKGROUP_SIZE_X),
        GetDispatchSize(mAmbientOcclusionImageExtent.height, RENDERER_SSAO_WORKGROUP_SIZE_Y),
        1
    );
}

void Renderer::ShadowCullPass(VkCommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mShadowCullPipeline.pipeline);

    const PushConstantsShadow pushConstants = {
        .shadowCascadeIdx = 0,
        .renderPassFlags = RENDER_PASS_OPAQUE_BIT,
    };
    vkCmdPushConstants(
        cb,
        mShadowCullPipeline.layout,
        VK_SHADER_STAGE_ALL,
        0,
        sizeof(pushConstants),
        &pushConstants
    );

    Vulkan::CmdPushDescriptors(
        cb,
        mShadowCullPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mDrawDataBuffer.buffer,
            mDrawCountBuffer.buffer,
            mDrawCmdBuffer1.buffer,
            mDrawCmdShadowBuffer.buffer,
            mDrawIndicesShadowBuffer.buffer,
        }
    );

    vkCmdDispatch(cb, GetDispatchSize(mUniformData.drawCount, RENDERER_CULL_WORKGROUP_SIZE), 1, 1);
}

void Renderer::ShadowPass(VkCommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mShadowPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mShadowPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mDrawIndicesShadowBuffer.buffer,
            mDrawDataBuffer.buffer,
            mVertexBuffer.buffer,
        }
    );

    VkRenderingAttachmentInfo depthAttachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = mShadowImageViewCascade[0],
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {{{0.0f, 0}}},
    };

    const VkRect2D renderArea = {
        .extent = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS},
    };

    const VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = renderArea,
        .layerCount = 1,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    const VkViewport viewport = {
        .width = f32(RENDERER_SHADOW_MAP_DIMENSIONS),
        .height = f32(RENDERER_SHADOW_MAP_DIMENSIONS),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cb, 0, 1, &viewport);

    const VkRect2D scissor = {
        .extent = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS},
    };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    vkCmdBindIndexBuffer(cb, mIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    // TODO: check out VK_KHR_multiview.
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        depthAttachmentInfo.imageView = mShadowImageViewCascade[i];

        const PushConstantsShadow pushConstants = {
            .shadowCascadeIdx = i,
            .renderPassFlags = RENDER_PASS_OPAQUE_BIT,
        };
        vkCmdPushConstants(
            cb,
            mShadowPipeline.layout,
            VK_SHADER_STAGE_ALL,
            0,
            sizeof(pushConstants),
            &pushConstants
        );

        vkCmdBeginRendering(cb, &renderingInfo);

        vkCmdDrawIndexedIndirectCount(
            cb,
            mDrawCmdShadowBuffer.buffer,
            0,
            mDrawCountBuffer.buffer,
            0,
            mUniformData.drawCount,
            sizeof(VkDrawIndexedIndirectCommand)
        );

        vkCmdEndRendering(cb);
    }
}

void Renderer::AmbientOcclusionBlurPass(VkCommandBuffer cb, bool horizontal)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mAmbientOcclusionBlurPipeline.pipeline);

    // clang-format off
    const Vulkan::DescriptorInfo inImage = horizontal ?
        Vulkan::DescriptorInfo{
            mAmbientOcclusionImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        } :
        Vulkan::DescriptorInfo{
            mAmbientOcclusionBlurredHorizontalImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };

    const Vulkan::DescriptorInfo outImage = horizontal ?
        Vulkan::DescriptorInfo{
            mAmbientOcclusionBlurredHorizontalImage.view, VK_IMAGE_LAYOUT_GENERAL
        } :
        Vulkan::DescriptorInfo{
            mAmbientOcclusionBlurredVerticalImage.view, VK_IMAGE_LAYOUT_GENERAL
        };
    // clang-format on

    Vulkan::CmdPushDescriptors(
        cb,
        mAmbientOcclusionBlurPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            {mDepthViewQuarterResImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            inImage,
            outImage,
        }
    );

    const PushConstantsSsaoBlur pushConstants
        = horizontal ? PushConstantsSsaoBlur{1, 0} : PushConstantsSsaoBlur{0, 1};

    vkCmdPushConstants(
        cb,
        mAmbientOcclusionBlurPipeline.layout,
        VK_SHADER_STAGE_ALL,
        0,
        sizeof(pushConstants),
        &pushConstants
    );

    vkCmdDispatch(
        cb,
        GetDispatchSize(mAmbientOcclusionImageExtent.width, RENDERER_SSAO_BLUR_WORKGROUP_SIZE_X),
        GetDispatchSize(mAmbientOcclusionImageExtent.height, RENDERER_SSAO_BLUR_WORKGROUP_SIZE_Y),
        1
    );
}

void Renderer::AmbientOcclusionUpsamplePass(VkCommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(
        cb,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        mAmbientOcclusionUpsamplePipeline.pipeline
    );

    Vulkan::CmdPushDescriptors(
        cb,
        mAmbientOcclusionUpsamplePipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mNearestSampler,
            {mDepthImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mDepthViewQuarterResImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mAmbientOcclusionBlurredVerticalImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mAmbientOcclusionUpsampledImage.view, VK_IMAGE_LAYOUT_GENERAL},
        }
    );

    vkCmdDispatch(
        cb,
        GetDispatchSize(mRenderImageExtent.width, RENDERER_SSAO_UPSAMPLE_WORKGROUP_SIZE_X),
        GetDispatchSize(mRenderImageExtent.height, RENDERER_SSAO_UPSAMPLE_WORKGROUP_SIZE_Y),
        1
    );
}

void Renderer::RenderPass(VkCommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mVisibilityRenderPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mVisibilityRenderPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mDrawIndicesEarlyBuffer.buffer,
            mDrawIndicesLateBuffer.buffer,
            mDrawCmdEarlyBuffer2.buffer,
            mDrawCmdLateBuffer2.buffer,
            mDrawDataBuffer.buffer,
            mIndexBuffer.buffer,
            mVertexBuffer.buffer,
            mMaterialBuffer.buffer,
            mTextureSampler,
            mShadowSampler,
            mShadowPcfJitterSampler,
            {mShadowImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mShadowPcfJitterImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mVisibilityImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mAmbientOcclusionUpsampledImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mVelocityImage.view, VK_IMAGE_LAYOUT_GENERAL},
            {mRenderImage.view, VK_IMAGE_LAYOUT_GENERAL},
        }
    );

    vkCmdBindDescriptorSets(
        cb,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        mVisibilityRenderPipeline.layout,
        1,
        1,
        &mTextureDescriptorSet,
        0,
        nullptr
    );

    vkCmdDispatch(
        cb,
        GetDispatchSize(mRenderImageExtent.width, RENDERER_RENDER_WORKGROUP_SIZE_X),
        GetDispatchSize(mRenderImageExtent.height, RENDERER_RENDER_WORKGROUP_SIZE_Y),
        1
    );
}

void Renderer::TaaResolvePass(VkCommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mTaaResolvePipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mTaaResolvePipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            {mRenderImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mDepthImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mVelocityImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {
                mSwapchainRecreated || mRenderModeChanged
                    ? mFrame[mFrameIdx].resolvedRenderImage.view
                    : mFrame[mPrevFrameIdx].resolvedRenderImage.view,
                VK_IMAGE_LAYOUT_GENERAL,
            },
            {mFrame[mFrameIdx].resolvedRenderImage.view, VK_IMAGE_LAYOUT_GENERAL},
            mLinearSampler,
        }
    );

    vkCmdDispatch(
        cb,
        GetDispatchSize(mRenderImageExtent.width, RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_X),
        GetDispatchSize(mRenderImageExtent.height, RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_Y),
        1
    );
}

void Renderer::DebugDrawPass(VkCommandBuffer cb)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mDebugDrawFillCmdPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mDebugDrawFillCmdPipeline,
        {
            mDebugDrawCountBuffer.buffer,
            mDebugDrawCmdBuffer.buffer,
        }
    );

    vkCmdDispatch(cb, 1, 1, 1);

    Vulkan::CmdMemoryBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
            ),
        }
    );

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mDebugDrawRectPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mDebugDrawRectPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mDebugDrawCountBuffer.buffer,
            mDebugDrawRectBuffer.buffer,
        }
    );

    const VkViewport viewport = {
        .width = f32(mSwapchain.extent.width),
        .height = f32(mSwapchain.extent.height),
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cb, 0, 1, &viewport);

    const VkRect2D scissor = {
        .extent = mSwapchain.extent,
    };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    const VkRenderingAttachmentInfo renderingAttachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = mFrame[mFrameIdx].resolvedRenderImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    const VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = mSwapchain.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &renderingAttachmentInfo,
    };

    vkCmdBeginRendering(cb, &renderingInfo);

    vkCmdDrawIndirect(cb, mDebugDrawCmdBuffer.buffer, 0, 1, 0);

    vkCmdEndRendering(cb);
}

void Renderer::FullscreenPass(VkCommandBuffer cb, u32 imageIdx)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mFullscreenPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mFullscreenPipeline,
        {
            {mFrame[mFrameIdx].resolvedRenderImage.view, VK_IMAGE_LAYOUT_GENERAL},
            mLinearSampler,
        }
    );

    const VkViewport viewport = {
        .y = f32(mSwapchain.extent.height),
        .width = f32(mSwapchain.extent.width),
        .height = -f32(mSwapchain.extent.height),
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cb, 0, 1, &viewport);

    const VkRect2D scissor = {
        .extent = mSwapchain.extent,
    };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    const VkRenderingAttachmentInfo renderingAttachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = mSwapchain.images[imageIdx].view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    const VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = mSwapchain.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &renderingAttachmentInfo,
    };

    vkCmdBeginRendering(cb, &renderingInfo);

    vkCmdDraw(cb, 3, 1, 0, 0);

    vkCmdEndRendering(cb);
}

void Renderer::DebugDrawGradErrorPass(VkCommandBuffer cb, bool cullLate, u32 imageIdx)
{
    DEBUG_ASSERT(cb);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mDebugGradErrorPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mDebugGradErrorPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            cullLate ? mDrawIndicesLateBuffer.buffer : mDrawIndicesEarlyBuffer.buffer,
            cullLate ? mDrawCmdLateBuffer2.buffer : mDrawCmdEarlyBuffer2.buffer,
            mDrawDataBuffer.buffer,
            mIndexBuffer.buffer,
            mVertexBuffer.buffer,
        }
    );

    const VkViewport viewport = {
        .y = f32(mSwapchain.extent.height),
        .width = f32(mSwapchain.extent.width),
        .height = -f32(mSwapchain.extent.height),
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cb, 0, 1, &viewport);

    const VkRect2D scissor = {
        .extent = mSwapchain.extent,
    };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    const VkRenderingAttachmentInfo renderingAttachmentInfos[] = {
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = mSwapchain.images[imageIdx].view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = cullLate ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        },
    };

    const VkRenderingAttachmentInfo depthAttachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = mDepthImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = cullLate ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    const VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = mSwapchain.extent},
        .layerCount = 1,
        .colorAttachmentCount = ARRAY_SIZE(renderingAttachmentInfos),
        .pColorAttachments = renderingAttachmentInfos,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    vkCmdBeginRendering(cb, &renderingInfo);

    vkCmdBindIndexBuffer(cb, mIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirectCount(
        cb,
        cullLate ? mDrawCmdLateBuffer2.buffer : mDrawCmdEarlyBuffer2.buffer,
        0,
        mDrawCountBuffer.buffer,
        0,
        mUniformData.drawCount,
        sizeof(VkDrawIndexedIndirectCommand)
    );

    vkCmdEndRendering(cb);
}

bool Renderer::RecordCommandBufferDebugGradError(u32 imageIdx)
{
    Frame& frame = mFrame[mFrameIdx];

    const VkCommandBuffer cb = frame.commandBufferStart;

    VK_CHECK(vkResetCommandBuffer(cb, 0));

    const VkCommandBufferBeginInfo cmdBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cb, &cmdBeginInfo));

    Vulkan::CmdMemoryBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cb, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdMemoryBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    CullPass(cb, false);

    Vulkan::CmdBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
        }
    );

    DebugDrawGradErrorPass(cb, false, imageIdx);

    Vulkan::CmdImageMemoryBarrier(
        cb,
        {
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthPyramidImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    if (!mCullCameraFrozen)
    {
        DepthReducePass(cb);
    }

    Vulkan::CmdMemoryBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cb, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mDepthPyramidImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    CullPass(cb, true);

    Vulkan::CmdBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
        }
    );

    DebugDrawGradErrorPass(cb, true, imageIdx);

    if (!mImguiRenderer.UpdateVertexIndexBuffers(static_cast<u32>(mFrameIdx)))
    {
        return false;
    }

    // TODO: separate pass.
    if (mEnableUI)
    {
        const VkRenderingAttachmentInfo renderingAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = mSwapchain.images[imageIdx].view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };

        const VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = mSwapchain.extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &renderingAttachmentInfo,
        };

        vkCmdBeginRendering(cb, &renderingInfo);

        if (!mImguiRenderer.Render(cb, u32(mFrameIdx)))
        {
            return false;
        }

        vkCmdEndRendering(cb);
    }

    Vulkan::CmdImageMemoryBarrier(
        cb,
        {
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE
            ),
        }
    );

    VK_CHECK(vkEndCommandBuffer(cb));

    return true;
}

bool Renderer::RecordCommandBufferVisibility(u32 imageIdx)
{
    Frame& frame = mFrame[mFrameIdx];

    const VkCommandBuffer cbStart = frame.commandBufferStart;
    const VkCommandBuffer cbShadow = frame.commandBufferShadow;
    const VkCommandBuffer cbEnd = frame.commandBufferEnd;
    const VkCommandBuffer cbSSAO = frame.commandBufferSSAO;

    VK_CHECK(vkResetCommandBuffer(cbStart, 0));
    VK_CHECK(vkResetCommandBuffer(cbShadow, 0));
    VK_CHECK(vkResetCommandBuffer(cbEnd, 0));
    VK_CHECK(vkResetCommandBuffer(cbSSAO, 0));

    const VkCommandBufferBeginInfo cmdBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(cbStart, &cmdBeginInfo));

    vkCmdFillBuffer(cbStart, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);
    vkCmdFillBuffer(cbStart, mDebugDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdMemoryBarrier(
        cbStart,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    CullPass(cbStart, false);

    Vulkan::CmdBarrier(
        cbStart,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mVisibilityImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
        }
    );

    VisibilityBufferPass(cbStart, false);

    Vulkan::CmdImageMemoryBarrier(
        cbStart,
        {
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthPyramidImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    if (!mCullCameraFrozen)
    {
        DepthReducePass(cbStart);
    }

    Vulkan::CmdMemoryBarrier(
        cbStart,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cbStart, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdBarrier(
        cbStart,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mDepthPyramidImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    CullPass(cbStart, true);

    Vulkan::CmdBarrier(
        cbStart,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
        }
    );

    VisibilityBufferPass(cbStart, true);

    Vulkan::CmdImageMemoryBarrier(
        cbStart,
        {
            Vulkan::ImageMemoryBarrier(
                mVisibilityImage.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_REMAINING_MIP_LEVELS,
                VK_REMAINING_ARRAY_LAYERS,
                mDevice.mGraphicsQueueInfo.familyIdx,
                mDevice.mComputeQueueInfo.familyIdx
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_REMAINING_MIP_LEVELS,
                VK_REMAINING_ARRAY_LAYERS,
                mDevice.mGraphicsQueueInfo.familyIdx,
                mDevice.mComputeQueueInfo.familyIdx
            ),
        }
    );

    VK_CHECK(vkEndCommandBuffer(cbStart));

    VK_CHECK(vkBeginCommandBuffer(cbSSAO, &cmdBeginInfo));

    Vulkan::CmdImageMemoryBarrier(
        cbSSAO,
        {
            Vulkan::ImageMemoryBarrier(
                mVisibilityImage.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_REMAINING_MIP_LEVELS,
                VK_REMAINING_ARRAY_LAYERS,
                mDevice.mGraphicsQueueInfo.familyIdx,
                mDevice.mComputeQueueInfo.familyIdx
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_REMAINING_MIP_LEVELS,
                VK_REMAINING_ARRAY_LAYERS,
                mDevice.mGraphicsQueueInfo.familyIdx,
                mDevice.mComputeQueueInfo.familyIdx
            ),
            Vulkan::ImageMemoryBarrier(
                mAmbientOcclusionImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthViewQuarterResImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    DepthViewQuarterResPass(cbSSAO);

    Vulkan::CmdImageMemoryBarrier(
        cbSSAO,
        {
            Vulkan::ImageMemoryBarrier(
                mDepthViewQuarterResImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    AmbientOcclusionPass(cbSSAO);

    Vulkan::CmdImageMemoryBarrier(
        cbSSAO,
        {
            Vulkan::ImageMemoryBarrier(
                mAmbientOcclusionImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mAmbientOcclusionBlurredHorizontalImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    AmbientOcclusionBlurPass(cbSSAO, true);

    Vulkan::CmdImageMemoryBarrier(
        cbSSAO,
        {
            Vulkan::ImageMemoryBarrier(
                mAmbientOcclusionBlurredHorizontalImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mAmbientOcclusionBlurredVerticalImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    AmbientOcclusionBlurPass(cbSSAO, false);

    Vulkan::CmdImageMemoryBarrier(
        cbSSAO,
        {
            Vulkan::ImageMemoryBarrier(
                mAmbientOcclusionBlurredVerticalImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mAmbientOcclusionUpsampledImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    AmbientOcclusionUpsamplePass(cbSSAO);

    Vulkan::CmdImageMemoryBarrier(
        cbSSAO,
        {
            Vulkan::ImageMemoryBarrier(
                mVisibilityImage.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_REMAINING_MIP_LEVELS,
                VK_REMAINING_ARRAY_LAYERS,
                mDevice.mComputeQueueInfo.familyIdx,
                mDevice.mGraphicsQueueInfo.familyIdx
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_REMAINING_MIP_LEVELS,
                VK_REMAINING_ARRAY_LAYERS,
                mDevice.mComputeQueueInfo.familyIdx,
                mDevice.mGraphicsQueueInfo.familyIdx
            ),
            Vulkan::ImageMemoryBarrier(
                mAmbientOcclusionUpsampledImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_REMAINING_MIP_LEVELS,
                VK_REMAINING_ARRAY_LAYERS,
                mDevice.mComputeQueueInfo.familyIdx,
                mDevice.mGraphicsQueueInfo.familyIdx
            ),
        }
    );

    VK_CHECK(vkEndCommandBuffer(cbSSAO));

    VK_CHECK(vkBeginCommandBuffer(cbShadow, &cmdBeginInfo));

    vkCmdFillBuffer(cbShadow, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdMemoryBarrier(
        cbShadow,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    ShadowCullPass(cbShadow);

    Vulkan::CmdBarrier(
        cbShadow,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mShadowImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_REMAINING_MIP_LEVELS,
                RENDERER_SHADOW_MAP_CASCADE_COUNT
            ),
        }
    );

    ShadowPass(cbShadow);

    VK_CHECK(vkEndCommandBuffer(cbShadow));

    VK_CHECK(vkBeginCommandBuffer(cbEnd, &cmdBeginInfo));

    Vulkan::CmdImageMemoryBarrier(
        cbEnd,
        {
            Vulkan::ImageMemoryBarrier(
                mVisibilityImage.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_REMAINING_MIP_LEVELS,
                VK_REMAINING_ARRAY_LAYERS,
                mDevice.mComputeQueueInfo.familyIdx,
                mDevice.mGraphicsQueueInfo.familyIdx
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_REMAINING_MIP_LEVELS,
                VK_REMAINING_ARRAY_LAYERS,
                mDevice.mComputeQueueInfo.familyIdx,
                mDevice.mGraphicsQueueInfo.familyIdx
            ),
            Vulkan::ImageMemoryBarrier(
                mAmbientOcclusionUpsampledImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_REMAINING_MIP_LEVELS,
                VK_REMAINING_ARRAY_LAYERS,
                mDevice.mComputeQueueInfo.familyIdx,
                mDevice.mGraphicsQueueInfo.familyIdx
            ),
            Vulkan::ImageMemoryBarrier(
                mRenderImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mVelocityImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mShadowImage.image,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_REMAINING_MIP_LEVELS,
                RENDERER_SHADOW_MAP_CASCADE_COUNT
            ),
        }
    );

    RenderPass(cbEnd);

    Vulkan::CmdImageMemoryBarrier(
        cbEnd,
        {
            Vulkan::ImageMemoryBarrier(
                mRenderImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                frame.resolvedRenderImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mVelocityImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    TaaResolvePass(cbEnd);

    Vulkan::CmdImageMemoryBarrier(
        cbEnd,
        {
            Vulkan::ImageMemoryBarrier(
                frame.resolvedRenderImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
        }
    );

    DebugDrawPass(cbEnd);

    Vulkan::CmdImageMemoryBarrier(
        cbEnd,
        {
            Vulkan::ImageMemoryBarrier(
                frame.resolvedRenderImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    FullscreenPass(cbEnd, imageIdx);

    if (!mImguiRenderer.UpdateVertexIndexBuffers(static_cast<u32>(mFrameIdx)))
    {
        return false;
    }

    if (mEnableUI)
    {
        const VkRenderingAttachmentInfo renderingAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = mSwapchain.images[imageIdx].view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };

        const VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = mSwapchain.extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &renderingAttachmentInfo,
        };

        vkCmdBeginRendering(cbEnd, &renderingInfo);

        if (!mImguiRenderer.Render(cbEnd, u32(mFrameIdx)))
        {
            return false;
        }

        vkCmdEndRendering(cbEnd);
    }

    Vulkan::CmdImageMemoryBarrier(
        cbEnd,
        {
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE
            ),
        }
    );

    VK_CHECK(vkEndCommandBuffer(cbEnd));

    return true;
}

bool Renderer::RecordCommandBufferForward(u32 imageIdx)
{
    Frame& frame = mFrame[mFrameIdx];

    const VkCommandBuffer cb = frame.commandBufferStart;

    VK_CHECK(vkResetCommandBuffer(cb, 0));

    const VkCommandBufferBeginInfo cmdBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cb, &cmdBeginInfo));

    Vulkan::CmdMemoryBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cb, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);
    vkCmdFillBuffer(cb, mDebugDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdMemoryBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    CullPass(cb, false);

    Vulkan::CmdBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mRenderImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mVelocityImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
        }
    );

    ForwardPass(cb, false);

    Vulkan::CmdImageMemoryBarrier(
        cb,
        {
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthPyramidImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    if (!mCullCameraFrozen)
    {
        DepthReducePass(cb);
    }

    Vulkan::CmdMemoryBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cb, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mDepthPyramidImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    CullPass(cb, true);

    Vulkan::CmdBarrier(
        cb,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            ),
        },
        {},
        {
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
        }
    );

    ForwardPass(cb, true);

    Vulkan::CmdImageMemoryBarrier(
        cb,
        {
            Vulkan::ImageMemoryBarrier(
                mRenderImage.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mDepthImage.image,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                frame.resolvedRenderImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mVelocityImage.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    TaaResolvePass(cb);

    Vulkan::CmdImageMemoryBarrier(
        cb,
        {
            Vulkan::ImageMemoryBarrier(
                frame.resolvedRenderImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            ),
        }
    );

    DebugDrawPass(cb);

    Vulkan::CmdImageMemoryBarrier(
        cb,
        {
            Vulkan::ImageMemoryBarrier(
                frame.resolvedRenderImage.image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    FullscreenPass(cb, imageIdx);

    if (!mImguiRenderer.UpdateVertexIndexBuffers(static_cast<u32>(mFrameIdx)))
    {
        return false;
    }

    if (mEnableUI)
    {
        const VkRenderingAttachmentInfo renderingAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = mSwapchain.images[imageIdx].view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };

        const VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = mSwapchain.extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &renderingAttachmentInfo,
        };

        vkCmdBeginRendering(cb, &renderingInfo);

        if (!mImguiRenderer.Render(cb, u32(mFrameIdx)))
        {
            return false;
        }

        vkCmdEndRendering(cb);
    }

    Vulkan::CmdImageMemoryBarrier(
        cb,
        {
            Vulkan::ImageMemoryBarrier(
                mSwapchain.images[imageIdx].image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE
            ),
        }
    );

    VK_CHECK(vkEndCommandBuffer(cb));

    return true;
}

bool Renderer::CreateSwapchain()
{
    if (!mDevice.DeviceWaitIdle())
    {
        return false;
    }

    CleanupSwapchain();
    CleanupColorResources();
    CleanupDepthResources();

    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        mDevice.mPhysicalDevice,
        mSurface,
        &surfaceCapabilities
    ));
    if (surfaceCapabilities.currentExtent.width != UINT32_MAX)
    {
        mSwapchain.extent = surfaceCapabilities.currentExtent;
    }
    else
    {
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSizeInPixels(mWindow, &width, &height))
        {
            SDL_PRINT_ERROR("SDL_GetWindowSizeInPixels");
            return false;
        }
        DEBUG_ASSERT(width > 0);
        DEBUG_ASSERT(height > 0);
        mSwapchain.extent.width = Clamp(
            u32(width),
            surfaceCapabilities.minImageExtent.width,
            surfaceCapabilities.maxImageExtent.width
        );
        mSwapchain.extent.height = Clamp(
            u32(height),
            surfaceCapabilities.minImageExtent.height,
            surfaceCapabilities.maxImageExtent.height
        );
    }

    mUniformData.swapchainWidth = mSwapchain.extent.width;
    mUniformData.swapchainHeight = mSwapchain.extent.height;
    mUniformData.viewToClip = Perspective(
        FOV_Y_RAD,
        f32(mSwapchain.extent.width) / f32(mSwapchain.extent.height),
        RENDERER_NEAR_PLANE
    );
    mUniformData.viewToClipInv0011 = {
        1.0f / mUniformData.viewToClip(0, 0),
        1.0f / mUniformData.viewToClip(1, 1),
    };
    mUniformData.prevWorldToClip = mUniformData.worldToClip;
    mUniformData.worldToClip = mUniformData.viewToClip * mUniformData.worldToView;
    mUniformData.clipToWorld = Inverse(mUniformData.worldToClip);

    u32 surfaceFormatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        mDevice.mPhysicalDevice,
        mSurface,
        &surfaceFormatCount,
        nullptr
    );
    DEBUG_ASSERT(surfaceFormatCount > 0);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        mDevice.mPhysicalDevice,
        mSurface,
        &surfaceFormatCount,
        surfaceFormats.data()
    );

    // TODO: support HDR when this gets merged:
    // https://forums.developer.nvidia.com/t/vulkan-extensions-needed-for-hdr-is-missing/334268/13
    bool swapchainSurfaceFormatFound = false;

    for (u32 i = 0; i < surfaceFormatCount; ++i)
    {
        const VkFormat format = surfaceFormats[i].format;
        if ((format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM)
            && (surfaceFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
        {
            mSwapchain.surfaceFormat = surfaceFormats[i];
            swapchainSurfaceFormatFound = true;
            break;
        }
    }
    if (!swapchainSurfaceFormatFound)
    {
        fprintf(stderr, "vulkan: failed to find a suitable swapchain surface format\n");
        return false;
    }

    mSwapchain.minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    const u32 maxImageCount = surfaceCapabilities.maxImageCount;
    if (surfaceCapabilities.maxImageCount > 0 && maxImageCount < mSwapchain.minImageCount)
    {
        mSwapchain.minImageCount = maxImageCount;
    }

    // The spec guarantees that at least one bit will be set.
    VkCompositeAlphaFlagBitsKHR surfaceCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
    {
        surfaceCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    else if (
        surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
    )
    {
        surfaceCompositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    else if (
        surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
    )
    {
        surfaceCompositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }

    const VkSwapchainCreateInfoKHR swapchainInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = mSurface,
        .minImageCount = mSwapchain.minImageCount,
        .imageFormat = mSwapchain.surfaceFormat.format,
        .imageColorSpace = mSwapchain.surfaceFormat.colorSpace,
        .imageExtent = mSwapchain.extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = surfaceCompositeAlpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };

    VK_CHECK(vkCreateSwapchainKHR(mDevice.mDevice, &swapchainInfo, nullptr, &mSwapchain.swapchain));

    u32 swapchainImageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(
        mDevice.mDevice,
        mSwapchain.swapchain,
        &swapchainImageCount,
        nullptr
    ));
    std::vector<VkImage> images(swapchainImageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(
        mDevice.mDevice,
        mSwapchain.swapchain,
        &swapchainImageCount,
        images.data()
    ));

    mSwapchain.images.resize(swapchainImageCount);
    for (u32 i = 0; i < swapchainImageCount; ++i)
    {
        mSwapchain.images[i].image = images[i];
    }

    // Creating image views for every swapchain image.
    VkImageViewCreateInfo imageViewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = mSwapchain.surfaceFormat.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    for (u32 i = 0; i < swapchainImageCount; ++i)
    {
        imageViewInfo.image = mSwapchain.images[i].image;
        VkImageView imageView{};
        VK_CHECK(vkCreateImageView(mDevice.mDevice, &imageViewInfo, nullptr, &imageView));
        mSwapchain.images[i].view = imageView;
    }

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
    for (size_t i = 0; i < mSwapchain.images.size(); ++i)
    {
        vkDestroyImageView(mDevice.mDevice, mSwapchain.images[i].view, nullptr);
        mSwapchain.images[i].view = VK_NULL_HANDLE;
    }
    vkDestroySwapchainKHR(mDevice.mDevice, mSwapchain.swapchain, nullptr);
    mSwapchain.swapchain = VK_NULL_HANDLE;
}

bool Renderer::CreateColorResources()
{
    mRenderImageExtent.width = mSwapchain.extent.width * RENDER_SCALE;
    mRenderImageExtent.height = mSwapchain.extent.height * RENDER_SCALE;

    mUniformData.renderWidth = mRenderImageExtent.width;
    mUniformData.renderHeight = mRenderImageExtent.height;
    mUniformData.aspect = f32(mUniformData.renderWidth) / f32(mUniformData.renderHeight);
    mUniformData.renderImageSizeInv
        = {1.0f / mUniformData.renderWidth, 1.0f / mUniformData.renderHeight};

    if (!mDevice.CreateImage({
            .image = mRenderImage,
            .formats = {
                VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
                VK_FORMAT_B10G11R11_UFLOAT_PACK32,
            },
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = mRenderImageExtent.width,
            .height = mRenderImageExtent.height,
            .debugName = "RenderImage",
        }))
    {
        return false;
    }

    if (!mDevice.CreateImage({
            .image = mVisibilityImage,
            .formats = {VK_FORMAT_R32G32_UINT},
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = mRenderImageExtent.width,
            .height = mRenderImageExtent.height,
            .debugName = "VisibilityImage",
        }))
    {
        return false;
    }

    mAmbientOcclusionImageExtent = {mRenderImageExtent.width / 2, mRenderImageExtent.height / 2};

    mUniformData.ambientOcclusionWidth = mAmbientOcclusionImageExtent.width;
    mUniformData.ambientOcclusionHeight = mAmbientOcclusionImageExtent.height;
    mUniformData.ambientOcclusionImageSizeInv
        = {1.0f / mUniformData.ambientOcclusionWidth, 1.0f / mUniformData.ambientOcclusionHeight};

    if (!mDevice.CreateImage({
            .image = mDebugImage,
            .formats = {VK_FORMAT_R32G32B32A32_SFLOAT},
            .usage = VK_IMAGE_USAGE_STORAGE_BIT,
            .width = mRenderImageExtent.width / 2,
            .height = mRenderImageExtent.height / 2,
            .debugName = "DebugImage",
        }))
    {
        return false;
    }

    if (!mDevice.CreateImage({
            .image = mAmbientOcclusionImage,
            .formats = {VK_FORMAT_R8_UNORM},
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = mAmbientOcclusionImageExtent.width,
            .height = mAmbientOcclusionImageExtent.height,
            .debugName = "AmbientOcclusionImage",
        }))
    {
        return false;
    }

    if (!mDevice.CreateImage({
            .image = mAmbientOcclusionBlurredHorizontalImage,
            .formats = {mAmbientOcclusionImage.format},
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = mAmbientOcclusionImageExtent.width,
            .height = mAmbientOcclusionImageExtent.height,
            .debugName = "AmbientOcclusionBlurredHorizontalImage",
        }))
    {
        return false;
    }

    if (!mDevice.CreateImage({
            .image = mAmbientOcclusionBlurredVerticalImage,
            .formats = {mAmbientOcclusionImage.format},
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = mAmbientOcclusionImageExtent.width,
            .height = mAmbientOcclusionImageExtent.height,
            .debugName = "AmbientOcclusionBlurredVerticalImage",
        }))
    {
        return false;
    }

    if (!mDevice.CreateImage({
            .image = mAmbientOcclusionUpsampledImage,
            .formats = {mAmbientOcclusionImage.format},
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = mRenderImageExtent.width,
            .height = mRenderImageExtent.height,
            .debugName = "AmbientOcclusionBlurredUpsampledImage",
        }))
    {
        return false;
    }

    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (!mDevice.CreateImage({
                .image = mFrame[i].resolvedRenderImage,
                .formats = {mRenderImage.format},
                .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                    | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .width = mRenderImageExtent.width,
                .height = mRenderImageExtent.height,
                .debugName = "ResolvedRenderImage",
            }))
        {
            return false;
        }
    }

    if (!mDevice.CreateImage({
            .image = mVelocityImage,
            .formats = {VK_FORMAT_R16G16_SFLOAT},
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT
                | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = mRenderImageExtent.width,
            .height = mRenderImageExtent.height,
            .debugName = "VelocityImage",
        }))
    {
        return false;
    }

    return true;
}

void Renderer::CleanupColorResources()
{
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        mDevice.DestroyImage(mFrame[i].resolvedRenderImage);
    }
    mDevice.DestroyImage(mAmbientOcclusionUpsampledImage);
    mDevice.DestroyImage(mAmbientOcclusionBlurredVerticalImage);
    mDevice.DestroyImage(mAmbientOcclusionBlurredHorizontalImage);
    mDevice.DestroyImage(mAmbientOcclusionImage);
    mDevice.DestroyImage(mVisibilityImage);
    mDevice.DestroyImage(mRenderImage);
    mDevice.DestroyImage(mVelocityImage);
    mDevice.DestroyImage(mDebugImage);
}

bool Renderer::CreateDepthResources()
{
    if (!mDevice.CreateImage({
            .image = mDepthImage,
            .formats = {VK_FORMAT_D32_SFLOAT},
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = mRenderImageExtent.width,
            .height = mRenderImageExtent.height,
            .debugName = "DepthImage",
        }))
    {
        return false;
    }

    if (!mDevice.CreateImage({
            .image = mDepthViewQuarterResImage,
            .formats = {VK_FORMAT_R16_SFLOAT},
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = mRenderImageExtent.width / 2,
            .height = mRenderImageExtent.height / 2,
            .debugName = "DepthViewQuarterResImage",
        }))
    {
        return false;
    }

    // PreviousPow2 to make reductions at most by 2x2, otherwise they are not conservative.
    mDepthPyramidImageExtent.width = PreviousPow2(mRenderImageExtent.width);
    mDepthPyramidImageExtent.height = PreviousPow2(mRenderImageExtent.height);

    mUniformData.depthPyramidWidth = f32(mDepthPyramidImageExtent.width);
    mUniformData.depthPyramidHeight = f32(mDepthPyramidImageExtent.height);

    const u32 depthPyramidMipLevels
        = Utils::GetMipLevels(mDepthPyramidImageExtent.width, mDepthPyramidImageExtent.height);

    if (!mDevice.CreateImage({
            .image = mDepthPyramidImage,
            .formats = {VK_FORMAT_R32_SFLOAT},
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .width = mDepthPyramidImageExtent.width,
            .height = mDepthPyramidImageExtent.height,
            .mipLevels = depthPyramidMipLevels,
            .debugName = "DepthPyramidImage",
        }))
    {
        return false;
    }

    mDepthPyramidMipImageViews.resize(depthPyramidMipLevels);

    for (size_t i = 0; i < mDepthPyramidMipImageViews.size(); ++i)
    {
        const VkImageViewCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = mDepthPyramidImage.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = mDepthPyramidImage.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = u32(i),
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        vkCreateImageView(mDevice.mDevice, &info, nullptr, &mDepthPyramidMipImageViews[i]);
    }

    return true;
}

void Renderer::CleanupDepthResources()
{
    mDevice.DestroyImage(mDepthImage);
    mDevice.DestroyImage(mDepthViewQuarterResImage);
    mDevice.DestroyImage(mDepthPyramidImage);
    for (VkImageView& view : mDepthPyramidMipImageViews)
    {
        vkDestroyImageView(mDevice.mDevice, view, nullptr);
        view = VK_NULL_HANDLE;
    }
}
