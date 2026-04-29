#include "ImguiRenderer.hpp"

#include "Vulkan.hpp"
#include "../Math/Utils.hpp"

#include <SDL3/SDL_video.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

// Heavily based on Sascha Willems's vulkan examples:
// https://github.com/SaschaWillems/Vulkan/blob/master/base/VulkanUIOverlay.h

bool ImguiRenderer::Init(
    SDL_Window* window,
    Vulkan::Device device,
    VkCommandPool commandPool,
    VkFormat colorFormat
)
{
    DEBUG_ASSERT(window);
    DEBUG_ASSERT(commandPool);

    mDevice = device;
    mCommandPool = commandPool;

    if (!IMGUI_CHECKVERSION())
    {
        fprintf(stderr, "IMGUI_CHECKVERSION failed\n");
        return false;
    }
    if (!ImGui::CreateContext())
    {
        fprintf(stderr, "ImGui::CreateContext failed\n");
        return false;
    }
    if (!ImGui_ImplSDL3_InitForVulkan(window))
    {
        fprintf(stderr, "ImGui_ImplSDL3_InitForVulkan failed\n");
        return 1;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    uchar* fontData{};
    int texWidth = 0;
    int texHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    const VkDeviceSize uploadSize = VkDeviceSize(texWidth * texHeight * 4) * sizeof(char);

    ImGui::StyleColorsDark();

    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (display == 0)
    {
        fprintf(stderr, "SDL_GetPrimaryDisplay failed: %s\n", SDL_GetError());
        return false;
    }
    const f32 windowScale = SDL_GetDisplayContentScale(display);
    if (windowScale == 0.0f)
    {
        fprintf(stderr, "SDL_GetDisplayContentScale failed: %s\n", SDL_GetError());
        return false;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(windowScale);
    style.FontScaleDpi = windowScale;

    // Font image.
    if (!mDevice.CreateImage({
            .image = mFontImage,
            .formats = {VK_FORMAT_R8G8B8A8_UNORM},
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .width = u32(texWidth),
            .height = u32(texHeight),
            .debugName = "FontImage",
        }))
    {
        return false;
    }

    // Uploading buffer data to font image.
    {
        Vulkan::Buffer stagingBuffer{};
        const bool result = mDevice.CreateBuffer({
            .buffer = stagingBuffer,
            .size = uploadSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .requiredFlags
            = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            .debugName = "StagingBuffer",
        });
        if (!result)
        {
            fprintf(stderr, "Vulkan failed to create a staging buffer\n");
            return false;
        }
        DEFER(mDevice.DestroyBuffer(stagingBuffer));

        memcpy(stagingBuffer.mapped, fontData, uploadSize);

        const VkCommandBufferAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = mCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer copyCmdBuffer{};
        VK_CHECK(vkAllocateCommandBuffers(mDevice.mDevice, &allocateInfo, &copyCmdBuffer));
        DEFER(vkFreeCommandBuffers(mDevice.mDevice, mCommandPool, 1, &copyCmdBuffer));

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        VK_CHECK(vkBeginCommandBuffer(copyCmdBuffer, &beginInfo));

        Vulkan::CmdImageMemoryBarrier(
            copyCmdBuffer,
            {
                Vulkan::ImageMemoryBarrier(
                    mFontImage.image,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_HOST_BIT,
                    VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT
                ),
            }
        );

        const VkImageSubresourceLayers imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        };

        const VkBufferImageCopy bufferCopyRegion = {
            .imageSubresource = imageSubresource,
            .imageExtent = {u32(texWidth), u32(texHeight), 1},
        };
        vkCmdCopyBufferToImage(
            copyCmdBuffer,
            stagingBuffer.buffer,
            mFontImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &bufferCopyRegion
        );

        Vulkan::CmdImageMemoryBarrier(
            copyCmdBuffer,
            {
                Vulkan::ImageMemoryBarrier(
                    mFontImage.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT
                ),
            }
        );

        VK_CHECK(vkEndCommandBuffer(copyCmdBuffer));

        if (!mDevice.QueueSubmit({
                .queueInfo = mDevice.mGraphicsQueueInfo,
                .commandBuffer = copyCmdBuffer,
            }))
        {
            return false;
        }
        if (!mDevice.QueueWaitIdle(mDevice.mGraphicsQueueInfo))
        {
            return false;
        }
    }

    // Font texture sampler.
    {
        const VkSamplerCreateInfo samplerInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        };
        VK_CHECK(vkCreateSampler(mDevice.mDevice, &samplerInfo, nullptr, &mFontSampler));
    }

    // Pipeline.
    {
        VkFormat stencilFormat{};
        if (!Vulkan::FindSupportedImageFormat(
                stencilFormat,
                mDevice.mPhysicalDevice,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                {VK_FORMAT_S8_UINT}
            ))
        {
            fprintf(stderr, "vulkan: failed to find a suitable imgui stencil attachment format\n");
            return false;
        }

        if (!mDevice.CreateGraphicsPipeline({
                .pipeline = mPipeline,
                .shaderPaths = {"Imgui.vert.hlsl.spv", "Imgui.frag.hlsl.spv"},
                .vertexBindingDescriptions = {
                    {0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX},
                },
                .vertexAttributeDescriptions = {
                    {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)},
                    {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)},
                    {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)},
                },
                .stencilFormat = stencilFormat,
                .colorAttachmentFormats = {colorFormat},
                .colorBlendAttachments = {
                    {
                        .blendEnable = VK_TRUE,
                        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                        .colorBlendOp = VK_BLEND_OP_ADD,
                        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                        .alphaBlendOp = VK_BLEND_OP_ADD,
                        .colorWriteMask = Vulkan::ColorComponentAllBits,
                    },
                },
                .dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
                .debugName = "FullscreenPass",
            }))
        {
            return false;
        }
    }

    return true;
}

void ImguiRenderer::Cleanup()
{
    if (!mDevice.mDevice)
    {
        return;
    }

    (void)mDevice.DeviceWaitIdle();

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        Frame& frame = mFrame[i];
        mDevice.DestroyBuffer(frame.vertexBuffer);
        mDevice.DestroyBuffer(frame.indexBuffer);
    }
    mDevice.DestroyPipeline(mPipeline);
    vkDestroySampler(mDevice.mDevice, mFontSampler, nullptr);
    vkDestroyImageView(mDevice.mDevice, mFontImage.view, nullptr);
    vmaDestroyImage(mDevice.mVmaAllocator, mFontImage.image, mFontImage.allocation);
}

bool ImguiRenderer::UpdateVertexIndexBuffers(u32 frameIndex)
{
    ImGui::Render();

    const ImDrawData* const drawData = ImGui::GetDrawData();
    if (!drawData)
    {
        return true;
    }

    VkDeviceSize vertexBufferSize = VkDeviceSize(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    VkDeviceSize indexBufferSize = VkDeviceSize(drawData->TotalIdxCount) * sizeof(ImDrawIdx);

    if (vertexBufferSize == 0 || indexBufferSize == 0)
    {
        return true;
    }

    Frame& frame = mFrame[frameIndex];

    // Round buffers up with multiple of a chunk size to minimize the need to recreate them.
    constexpr VkDeviceSize chunkSize = 16384;
    vertexBufferSize = ((vertexBufferSize + chunkSize - 1) / chunkSize) * chunkSize;
    indexBufferSize = ((indexBufferSize + chunkSize - 1) / chunkSize) * chunkSize;

    const bool shouldRecreateVertexBuffer = (frame.vertexBuffer.buffer == VK_NULL_HANDLE)
        || (frame.vertexBufferSize < vertexBufferSize);
    if (shouldRecreateVertexBuffer)
    {
        mDevice.DestroyBuffer(frame.vertexBuffer);

        if (!mDevice.CreateBuffer({
                .buffer = frame.vertexBuffer,
                .size = vertexBufferSize,
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                .debugName = "ImGuiVertexBuffer",
            }))
        {
            return false;
        }
        frame.vertexCount = drawData->TotalVtxCount;
        frame.vertexBufferSize = vertexBufferSize;
    }

    const bool shouldRecreateIndexBuffer
        = (frame.indexBuffer.buffer == VK_NULL_HANDLE) || (frame.indexBufferSize < indexBufferSize);
    if (shouldRecreateIndexBuffer)
    {
        mDevice.DestroyBuffer(frame.indexBuffer);

        if (!mDevice.CreateBuffer({
                .buffer = frame.indexBuffer,
                .size = indexBufferSize,
                .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                .debugName = "ImGuiIndexBuffer",
            }))
        {
            return false;
        }
        frame.indexCount = drawData->TotalIdxCount;
        frame.indexBufferSize = indexBufferSize;
    }

    // Upload data.
    ImDrawVert* vertexDst = static_cast<ImDrawVert*>(frame.vertexBuffer.mapped);
    ImDrawIdx* indexDst = static_cast<ImDrawIdx*>(frame.indexBuffer.mapped);
    for (int i = 0; i < drawData->CmdListsCount; ++i)
    {
        const ImDrawList* const cmdList = drawData->CmdLists[i];
        memcpy(
            vertexDst,
            cmdList->VtxBuffer.Data,
            VkDeviceSize(cmdList->VtxBuffer.Size) * sizeof(ImDrawVert)
        );
        memcpy(
            indexDst,
            cmdList->IdxBuffer.Data,
            VkDeviceSize(cmdList->IdxBuffer.Size) * sizeof(ImDrawIdx)
        );
        vertexDst += cmdList->VtxBuffer.Size;
        indexDst += cmdList->IdxBuffer.Size;
    }

    const VmaAllocation allocations[]
        = {frame.vertexBuffer.allocation, frame.indexBuffer.allocation};
    VK_CHECK(vmaFlushAllocations(
        mDevice.mVmaAllocator,
        ARRAY_SIZE(allocations),
        allocations,
        nullptr,
        nullptr
    ));

    return true;
}

void ImguiRenderer::StartNewFrame() const
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

bool ImguiRenderer::Render(VkCommandBuffer cb, u32 frameIndex)
{
    const ImDrawData* const drawData = ImGui::GetDrawData();
    i32 vertexOffset = 0;
    u32 indexOffset = 0;

    if (!drawData || drawData->CmdListsCount == 0)
    {
        return true;
    }

    Frame& frame = mFrame[frameIndex];

    if (!frame.vertexBuffer.buffer || !frame.indexBuffer.buffer)
    {
        return true;
    }

    const ImGuiIO& io = ImGui::GetIO();

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cb,
        mPipeline,
        {
            {mFontImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            mFontSampler,
        }
    );

    const PushConstantsImgui pushConstants = {
        .scale = Vec2{2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y},
        .translate = Vec2{-1.0f},
    };
    vkCmdPushConstants(
        cb,
        mPipeline.layout,
        VK_SHADER_STAGE_ALL,
        0,
        sizeof(pushConstants),
        &pushConstants
    );

    VkDeviceSize offsets[1]{};
    vkCmdBindVertexBuffers(cb, 0, 1, &frame.vertexBuffer.buffer, offsets);
    vkCmdBindIndexBuffer(cb, frame.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

    const VkViewport viewport = {
        .width = io.DisplaySize.x,
        .height = io.DisplaySize.y,
    };
    vkCmdSetViewport(cb, 0, 1, &viewport);

    for (int i = 0; i < drawData->CmdListsCount; ++i)
    {
        const ImDrawList* const cmdList = drawData->CmdLists[i];
        for (int j = 0; j < cmdList->CmdBuffer.Size; ++j)
        {
            const ImDrawCmd& imCmd = cmdList->CmdBuffer[j];
            const ImVec4 rect = imCmd.ClipRect;

            const VkRect2D scissorRect = {
                .offset = {Max(i32(rect.x), 0), Max(i32(rect.y), 0)},
                .extent = {u32(rect.z - rect.x), u32(rect.w - rect.y)},
            };
            vkCmdSetScissor(cb, 0, 1, &scissorRect);

            vkCmdDrawIndexed(cb, imCmd.ElemCount, 1, indexOffset, vertexOffset, 0);
            indexOffset += imCmd.ElemCount;
        }
        vertexOffset += cmdList->VtxBuffer.Size;
    }

    return true;
}
