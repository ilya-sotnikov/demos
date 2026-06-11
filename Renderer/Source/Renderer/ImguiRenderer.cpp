#include "ImguiRenderer.hpp"

#include "../Utils.hpp"
#include "../Math/Utils.hpp"

#include <SDL3/SDL_video.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <stdio.h>

// Heavily based on Sascha Willems's vulkan examples:
// https://github.com/SaschaWillems/Vulkan/blob/master/base/VulkanUIOverlay.h

bool ImguiRenderer::Init(SDL_Window* window, RHI::Format colorFormat)
{
    DEBUG_ASSERT(window);

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
    const u64 uploadSize = u64(texWidth * texHeight * 4) * sizeof(char);

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

    mFontTexture = RHI::CreateTexture({
        .format = RHI::FORMAT_R8G8B8A8_UNORM,
        .dimensions = {u32(texWidth), u32(texHeight), 1},
        .usage = RHI::TEXTURE_USAGE_SAMPLED_BIT | RHI::TEXTURE_USAGE_TRANSFER_DST_BIT,
        .debugName = "FontTexture",
    });
    if (!mFontTexture)
    {
        return false;
    }

    // Uploading buffer data to font texture.
    {
        RHI::BufferHandle stagingBuffer = RHI::CreateBuffer({
            .size = uploadSize,
            .debugName = "StagingBuffer",
        });
        if (!stagingBuffer)
        {
            return false;
        }
        DEFER(RHI::DestroyBuffer(stagingBuffer));

        memcpy(RHI::GetBufferHostPtr(stagingBuffer), fontData, uploadSize);

        const RHI::CommandBufferHandle cb = RHI::CreateCommandBuffer(RHI::QUEUE_GRAPHICS);
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
                mFontTexture,
                RHI::TEXTURE_LAYOUT_UNDEFINED,
                RHI::TEXTURE_LAYOUT_GENERAL,
                RHI::STAGE_HOST_BIT,
                RHI::ACCESS_NONE,
                RHI::STAGE_ALL_TRANSFER_BIT,
                RHI::ACCESS_TRANSFER_WRITE_BIT,
            }}
        );

        RHI::CmdCopyBufferToTexture(
            cb,
            stagingBuffer,
            mFontTexture,
            {{
                .textureDimensions = {u32(texWidth), u32(texHeight), 1},
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

        (void)RHI::QueueWaitIdle(RHI::QUEUE_GRAPHICS);
    }

    mFontSampler = RHI::CreateSampler({});
    if (!mFontSampler)
    {
        return false;
    }

    // Pipeline.
    {
        Utils::FileData vertData = Utils::FileRead("Imgui.vert.hlsl.spv");
        DEFER(free(vertData.data));
        Utils::FileData fragData = Utils::FileRead("Imgui.frag.hlsl.spv");
        DEFER(free(fragData.data));

        mPipeline = RHI::CreateGraphicsPipeline({
            .bytecodes = {
                {static_cast<u8*>(vertData.data), int(vertData.size)},
                {static_cast<u8*>(fragData.data), int(fragData.size)},
            },
            .stencilFormat = RHI::FORMAT_S8_UINT,
            .colorTargets = {{
                .format = colorFormat,
                .blendEnable = true,
                .srcColorFactor = RHI::BLEND_FACTOR_SRC_ALPHA,
                .dstColorFactor = RHI::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .srcAlphaFactor = RHI::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .dstAlphaFactor = RHI::BLEND_FACTOR_ZERO,
            }},
            .debugName = "FullscreenPass",
        });
        if (!mPipeline)
        {
            return false;
        }
    }

    return true;
}

void ImguiRenderer::Cleanup()
{
    (void)RHI::DeviceWaitIdle();

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    for (int i = 0; i < RHI::FRAMES_IN_FLIGHT; ++i)
    {
        Frame& frame = mFrame[i];
        RHI::DestroyBuffer(frame.vertexBuffer);
        RHI::DestroyBuffer(frame.indexBuffer);
    }
    RHI::DestroyPipeline(mPipeline);
    RHI::DestroySampler(mFontSampler);
    RHI::DestroyTexture(mFontTexture);
}

bool ImguiRenderer::UpdateVertexIndexBuffers(u32 frameIndex)
{
    ImGui::Render();

    const ImDrawData* const drawData = ImGui::GetDrawData();
    if (!drawData)
    {
        return true;
    }

    u64 vertexBufferSize = u64(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    u64 indexBufferSize = u64(drawData->TotalIdxCount) * sizeof(ImDrawIdx);

    if (vertexBufferSize == 0 || indexBufferSize == 0)
    {
        return true;
    }

    Frame& frame = mFrame[frameIndex];

    // Round buffers up with multiple of a chunk size to minimize the need to recreate them.
    const u64 chunkSize = 16384;
    vertexBufferSize = ((vertexBufferSize + chunkSize - 1) / chunkSize) * chunkSize;
    indexBufferSize = ((indexBufferSize + chunkSize - 1) / chunkSize) * chunkSize;

    const bool shouldRecreateVertexBuffer
        = !frame.vertexBuffer || (frame.vertexBufferSize < vertexBufferSize);
    if (shouldRecreateVertexBuffer)
    {
        RHI::DestroyBuffer(frame.vertexBuffer);

        frame.vertexBuffer = RHI::CreateBuffer({
            .size = vertexBufferSize,
            .debugName = "ImGuiVertexBuffer",
        });
        if (!frame.vertexBuffer)
        {
            return false;
        }
        frame.vertexCount = drawData->TotalVtxCount;
        frame.vertexBufferSize = vertexBufferSize;
    }

    const bool shouldRecreateIndexBuffer
        = !frame.indexBuffer || (frame.indexBufferSize < indexBufferSize);
    if (shouldRecreateIndexBuffer)
    {
        RHI::DestroyBuffer(frame.indexBuffer);

        frame.indexBuffer = RHI::CreateBuffer({
            .size = indexBufferSize,
            .debugName = "ImGuiIndexBuffer",
        });
        if (!frame.indexBuffer)
        {
            return false;
        }
        frame.indexCount = drawData->TotalIdxCount;
        frame.indexBufferSize = indexBufferSize;
    }

    // Upload data.
    ImDrawVert* vertexDst = static_cast<ImDrawVert*>(RHI::GetBufferHostPtr(frame.vertexBuffer));
    ImDrawIdx* indexDst = static_cast<ImDrawIdx*>(RHI::GetBufferHostPtr(frame.indexBuffer));
    for (int i = 0; i < drawData->CmdListsCount; ++i)
    {
        const ImDrawList* const cmdList = drawData->CmdLists[i];
        memcpy(
            vertexDst,
            cmdList->VtxBuffer.Data,
            u64(cmdList->VtxBuffer.Size) * sizeof(ImDrawVert)
        );
        memcpy(indexDst, cmdList->IdxBuffer.Data, u64(cmdList->IdxBuffer.Size) * sizeof(ImDrawIdx));
        vertexDst += cmdList->VtxBuffer.Size;
        indexDst += cmdList->IdxBuffer.Size;
    }

    return true;
}

void ImguiRenderer::StartNewFrame() const
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

bool ImguiRenderer::Render(RHI::CommandBufferHandle cb, u32 frameIndex)
{
    const ImDrawData* const drawData = ImGui::GetDrawData();
    i32 vertexOffset = 0;
    u32 indexOffset = 0;

    if (!drawData || drawData->CmdListsCount == 0)
    {
        return true;
    }

    Frame& frame = mFrame[frameIndex];

    if (!frame.vertexBuffer || !frame.indexBuffer)
    {
        return true;
    }

    const ImGuiIO& io = ImGui::GetIO();

    RHI::CmdBindPipeline(cb, mPipeline);

    RHI::CmdPushDescriptors(
        cb,
        mPipeline,
        {
            frame.vertexBuffer,
            mFontTexture,
            mFontSampler,
        }
    );

    const PushConstantsImgui pushConstants = {
        .scale = Vec2{2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y},
        .translate = Vec2{-1.0f},
    };
    RHI::CmdPushConstants(cb, mPipeline, &pushConstants);

    RHI::CmdBindIndexBuffer(cb, frame.indexBuffer, 0, RHI::INDEX_TYPE_U16);

    RHI::CmdSetViewport({
        .cb = cb,
        .width = io.DisplaySize.x,
        .height = io.DisplaySize.y,
    });

    for (int i = 0; i < drawData->CmdListsCount; ++i)
    {
        const ImDrawList* const cmdList = drawData->CmdLists[i];
        for (int j = 0; j < cmdList->CmdBuffer.Size; ++j)
        {
            const ImDrawCmd& imCmd = cmdList->CmdBuffer[j];
            const ImVec4 rect = imCmd.ClipRect;

            RHI::CmdSetScissor({
                .cb = cb,
                .offset = {Max(i32(rect.x), 0), Max(i32(rect.y), 0)},
                .extent = {u32(rect.z - rect.x), u32(rect.w - rect.y)},
            });

            RHI::CmdDrawIndexed(cb, imCmd.ElemCount, 1, indexOffset, vertexOffset, 0);
            indexOffset += imCmd.ElemCount;
        }
        vertexOffset += cmdList->VtxBuffer.Size;
    }

    return true;
}
