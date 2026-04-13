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
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VmaAllocator vmaAllocator,
    VkCommandPool commandPool,
    Vulkan::QueueInfo queueInfo,
    VkFormat colorFormat
)
{
    DEBUG_ASSERT(window);
    DEBUG_ASSERT(physicalDevice);
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(vmaAllocator);
    DEBUG_ASSERT(commandPool);
    DEBUG_ASSERT(queueInfo.queue);

    mPhysicalDevice = physicalDevice;
    mDevice = device;
    mVmaAllocator = vmaAllocator;
    mCommandPool = commandPool;
    mQueueInfo = queueInfo;

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
    {
        VkFormat fontImageFormat{};
        if (!Vulkan::FindSupportedImageFormat(
                fontImageFormat,
                physicalDevice,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                {VK_FORMAT_R8G8B8A8_UNORM}
            ))
        {
            fprintf(stderr, "vulkan: failed to find a suitable imgui font image format\n");
            return false;
        }

        const VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = fontImageFormat,
            .extent = {u32(texWidth), u32(texHeight), 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        };

        const VmaAllocationCreateInfo allocationInfo = {
            .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        };

        // TODO: why did I not use my own wrapper? Forgot to change it?
        VK_CHECK(vmaCreateImage(
            mVmaAllocator,
            &imageInfo,
            &allocationInfo,
            &mFontImage.image,
            &mFontImage.allocation,
            nullptr
        ));

        const VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = mFontImage.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = fontImageFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };

        VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mFontImage.view));
    }

    // Uploading buffer data to font image.
    {
        Vulkan::Buffer stagingBuffer{};
        const bool result = Vulkan::CreateBuffer(
            stagingBuffer,
            mDevice,
            mVmaAllocator,
            uploadSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            "stagingBuffer"
        );
        if (!result)
        {
            fprintf(stderr, "Vulkan failed to create a staging buffer\n");
            return false;
        }
        DEFER(Vulkan::DestroyBuffer(stagingBuffer, mVmaAllocator));

        memcpy(stagingBuffer.mapped, fontData, uploadSize);

        const VkCommandBufferAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = mCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer copyCmdBuffer{};
        VK_CHECK(vkAllocateCommandBuffers(mDevice, &allocateInfo, &copyCmdBuffer));
        DEFER(vkFreeCommandBuffers(mDevice, mCommandPool, 1, &copyCmdBuffer));

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

        const VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &copyCmdBuffer,
        };

        const VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence{};
        VK_CHECK(vkCreateFence(mDevice, &fenceInfo, nullptr, &fence));
        DEFER(vkDestroyFence(mDevice, fence, nullptr));

        VK_CHECK(vkQueueSubmit(queueInfo.queue, 1, &submitInfo, fence));

        VK_CHECK(vkWaitForFences(mDevice, 1, &fence, VK_TRUE, 1'000'000'000));
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
        VK_CHECK(vkCreateSampler(mDevice, &samplerInfo, nullptr, &mFontSampler));
    }

    // Pipeline.
    {
        const VkPushConstantRange pushConstantRange = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(PushConstantBlock),
        };

        const VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        const VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .lineWidth = 1.0f,
        };

        const VkPipelineColorBlendAttachmentState blendAttachmentState = {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };

        const VkVertexInputBindingDescription vertexInputBindingDescriptions[] = {
            {0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX},
        };
        const VkVertexInputAttributeDescription vertexInputAttributes[] = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)},
            {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)},
        };

        const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = ARRAY_SIZE(vertexInputBindingDescriptions),
            .pVertexBindingDescriptions = vertexInputBindingDescriptions,
            .vertexAttributeDescriptionCount = ARRAY_SIZE(vertexInputAttributes),
            .pVertexAttributeDescriptions = vertexInputAttributes,
        };

        const VkPipelineColorBlendStateCreateInfo colorBlendInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &blendAttachmentState,
        };

        const VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthCompareOp = VK_COMPARE_OP_ALWAYS,
            .back = {.compareOp = VK_COMPARE_OP_ALWAYS},
        };

        const VkPipelineViewportStateCreateInfo viewportInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        const VkPipelineMultisampleStateCreateInfo multisampleInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        const VkDynamicState dynamicStates[]
            = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        const VkPipelineDynamicStateCreateInfo dynamicInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = ARRAY_SIZE(dynamicStates),
            .pDynamicStates = dynamicStates,
        };

        VkFormat stencilFormat{};
        if (!Vulkan::FindSupportedImageFormat(
                stencilFormat,
                physicalDevice,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                {VK_FORMAT_S8_UINT}
            ))
        {
            fprintf(stderr, "vulkan: failed to find a suitable imgui stencil attachment format\n");
            return false;
        }

        const VkPipelineRenderingCreateInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &colorFormat,
            .stencilAttachmentFormat = stencilFormat,
        };

        VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &renderingInfo,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pViewportState = &viewportInfo,
            .pRasterizationState = &rasterizationInfo,
            .pMultisampleState = &multisampleInfo,
            .pDepthStencilState = &depthStencilInfo,
            .pColorBlendState = &colorBlendInfo,
            .pDynamicState = &dynamicInfo,
        };
        if (!Vulkan::CreateGraphicsPipeline(
                mPipeline,
                mDevice,
                {"Imgui.vert.hlsl.spv", "Imgui.frag.hlsl.spv"},
                VK_NULL_HANDLE,
                pipelineInfo,
                {},
                "ImguiPass"
            ))
        {
            return false;
        }
    }

    return true;
}

void ImguiRenderer::Cleanup()
{
    if (!mDevice)
    {
        return;
    }

    (void)vkDeviceWaitIdle(mDevice);

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        Frame& frame = mFrame[i];
        Vulkan::DestroyBuffer(frame.vertexBuffer, mVmaAllocator);
        Vulkan::DestroyBuffer(frame.indexBuffer, mVmaAllocator);
    }
    Vulkan::DestroyPipeline(mPipeline, mDevice);
    vkDestroySampler(mDevice, mFontSampler, nullptr);
    vkDestroyImageView(mDevice, mFontImage.view, nullptr);
    vmaDestroyImage(mVmaAllocator, mFontImage.image, mFontImage.allocation);
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
        Vulkan::DestroyBuffer(frame.vertexBuffer, mVmaAllocator);

        const bool result = Vulkan::CreateBuffer(
            frame.vertexBuffer,
            mDevice,
            mVmaAllocator,
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            "ImGuiVertexBuffer"
        );
        if (!result)
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
        Vulkan::DestroyBuffer(frame.indexBuffer, mVmaAllocator);

        const bool result = Vulkan::CreateBuffer(
            frame.indexBuffer,
            mDevice,
            mVmaAllocator,
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            "ImGuiIndexBuffer"
        );
        if (!result)
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
    VK_CHECK(
        vmaFlushAllocations(mVmaAllocator, ARRAY_SIZE(allocations), allocations, nullptr, nullptr)
    );

    return true;
}

void ImguiRenderer::StartNewFrame() const
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

bool ImguiRenderer::Render(VkCommandBuffer cmd, u32 frameIndex)
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
        mPipeline,
        {
            {
                mFontImage.view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                mFontSampler,
            },
        }
    );

    const PushConstantsImgui pushConstants = {
        .scale = Vec2{2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y},
        .translate = Vec2{-1.0f},
    };
    vkCmdPushConstants(
        cmd,
        mPipeline.layout,
        VK_SHADER_STAGE_ALL,
        0,
        sizeof(pushConstants),
        &pushConstants
    );

    VkDeviceSize offsets[1]{};
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer.buffer, offsets);
    vkCmdBindIndexBuffer(cmd, frame.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

    const VkViewport viewport = {
        .width = io.DisplaySize.x,
        .height = io.DisplaySize.y,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

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
            vkCmdSetScissor(cmd, 0, 1, &scissorRect);

            vkCmdDrawIndexed(cmd, imCmd.ElemCount, 1, indexOffset, vertexOffset, 0);
            indexOffset += imCmd.ElemCount;
        }
        vertexOffset += cmdList->VtxBuffer.Size;
    }

    return true;
}
