#include "ImGuiRenderer.hpp"

#include "Vulkan.hpp"
#include "../Math/Utils.hpp"

#include "SDL3/SDL_video.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"

// Heavily based on Sascha Willems's vulkan examples:
// https://github.com/SaschaWillems/Vulkan/blob/master/base/VulkanUIOverlay.h

bool ImGuiRenderer::Init(
    SDL_Window* window,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkCommandPool commandPool,
    Vulkan::QueueInfo queueInfo,
    VkFormat colorFormat,
    VkFormat depthFormat
)
{
    assert(window);

    mPhysicalDevice = physicalDevice;
    mDevice = device;
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
    const VkDeviceSize uploadSize
        = static_cast<VkDeviceSize>(texWidth * texHeight * 4) * sizeof(char);

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
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = {static_cast<u32>(texWidth), static_cast<u32>(texHeight), 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VK_CHECK(vkCreateImage(mDevice, &imageInfo, nullptr, &mFontImage.mImage));

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(mDevice, mFontImage.mImage, &memoryRequirements);

        u32 memoryTypeIndex = 0;
        const bool result = Vulkan::FindMemoryType(
            memoryTypeIndex,
            mPhysicalDevice,
            memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        if (!result)
        {
            fprintf(stderr, "Vulkan failed to find a suitable memory type\n");
            return false;
        }

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = memoryTypeIndex;
        VK_CHECK(vkAllocateMemory(mDevice, &allocateInfo, nullptr, &mFontImage.mMemory));

        VK_CHECK(vkBindImageMemory(mDevice, mFontImage.mImage, mFontImage.mMemory, 0));

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = 1;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = mFontImage.mImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = subresourceRange;
        VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mFontImage.mView));
    }

    // Uploading buffer data to font image.
    {
        VkBuffer stagingBuffer{};
        VkDeviceMemory stagingBufferMemory{};
        const bool result = Vulkan::CreateBuffer(
            stagingBuffer,
            stagingBufferMemory,
            mPhysicalDevice,
            mDevice,
            uploadSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        if (!result)
        {
            fprintf(stderr, "Vulkan failed to create a staging buffer\n");
            return false;
        }
        DEFER(vkDestroyBuffer(mDevice, stagingBuffer, nullptr));
        DEFER(vkFreeMemory(mDevice, stagingBufferMemory, nullptr));

        void* mapped{};
        VK_CHECK(vkMapMemory(mDevice, stagingBufferMemory, 0, VK_WHOLE_SIZE, 0, &mapped));
        memcpy(mapped, fontData, uploadSize);

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool = mCommandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VkCommandBuffer copyCmdBuffer{};
        VK_CHECK(vkAllocateCommandBuffers(mDevice, &allocateInfo, &copyCmdBuffer));
        DEFER(vkFreeCommandBuffers(mDevice, mCommandPool, 1, &copyCmdBuffer));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(copyCmdBuffer, &beginInfo));

        Vulkan::ImageMemoryBarrier(
            copyCmdBuffer,
            mFontImage.mImage,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        VkImageSubresourceLayers imageSubresource{};
        imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageSubresource.layerCount = 1;

        VkBufferImageCopy bufferCopyRegion{};
        bufferCopyRegion.imageSubresource = imageSubresource;
        bufferCopyRegion.imageExtent = {static_cast<u32>(texWidth), static_cast<u32>(texHeight), 1};
        vkCmdCopyBufferToImage(
            copyCmdBuffer,
            stagingBuffer,
            mFontImage.mImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &bufferCopyRegion
        );

        Vulkan::ImageMemoryBarrier(
            copyCmdBuffer,
            mFontImage.mImage,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT
        );

        VK_CHECK(vkEndCommandBuffer(copyCmdBuffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copyCmdBuffer;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence{};
        VK_CHECK(vkCreateFence(mDevice, &fenceInfo, nullptr, &fence));
        DEFER(vkDestroyFence(mDevice, fence, nullptr));

        VK_CHECK(vkQueueSubmit(queueInfo.mQueue, 1, &submitInfo, fence));

        VK_CHECK(vkWaitForFences(mDevice, 1, &fence, VK_TRUE, 1'000'000'000));
    }

    // Font texture sampler.
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK(vkCreateSampler(mDevice, &samplerInfo, nullptr, &mFontImage.mSampler));
    }

    // Descriptor pool.
    {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VK_CHECK(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mDescriptorPool));
    }

    // Descriptor set.
    VkDescriptorSetLayout descriptorSetLayout{};
    DEFER(vkDestroyDescriptorSetLayout(mDevice, descriptorSetLayout, nullptr));
    {
        // Descriptor set layout.
        VkDescriptorSetLayoutBinding setLayoutBinding{};
        setLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        setLayoutBinding.descriptorCount = 1;
        setLayoutBinding.binding = 0;
        setLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
        setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setLayoutInfo.bindingCount = 1;
        setLayoutInfo.pBindings = &setLayoutBinding;
        VK_CHECK(
            vkCreateDescriptorSetLayout(mDevice, &setLayoutInfo, nullptr, &descriptorSetLayout)
        );

        // Descriptor set.
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = mDescriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &descriptorSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(mDevice, &allocateInfo, &mDescriptorSet));

        VkDescriptorImageInfo fontDescriptor{};
        fontDescriptor.sampler = mFontImage.mSampler;
        fontDescriptor.imageView = mFontImage.mView;
        fontDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writeDescriptorSets{};
        writeDescriptorSets.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSets.descriptorCount = 1;
        writeDescriptorSets.dstBinding = 0;
        writeDescriptorSets.pImageInfo = &fontDescriptor;
        writeDescriptorSets.dstSet = mDescriptorSet;
        vkUpdateDescriptorSets(mDevice, 1, &writeDescriptorSets, 0, nullptr);
    }

    // Pipeline.
    {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.size = sizeof(PushConstantBlock);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        VK_CHECK(vkCreatePipelineLayout(mDevice, &pipelineLayoutInfo, nullptr, &mPipelineLayout));

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
        rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationInfo.lineWidth = 1.0f;

        VkPipelineColorBlendAttachmentState blendAttachmentState{};
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        // clang-format off
        constexpr VkVertexInputBindingDescription vertexInputBindingDescriptions[] = {
            {0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX},
        };
        constexpr VkVertexInputAttributeDescription vertexInputAttributes[] = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)},
            {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)},
        };
        // clang-format on

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.pVertexBindingDescriptions = vertexInputBindingDescriptions;
        vertexInputInfo.vertexBindingDescriptionCount = ARRAY_SIZE(vertexInputBindingDescriptions);
        vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributes;
        vertexInputInfo.vertexAttributeDescriptionCount = ARRAY_SIZE(vertexInputAttributes);

        VkShaderModule shaderModule{};

        const bool result
            = Vulkan::CreateShaderModule(shaderModule, mDevice, "ImGuiRenderer.slang.spv");
        if (!result)
        {
            fprintf(stderr, "Vulkan failed to build a vertex/fragment shader\n");
            return false;
        }
        DEFER(vkDestroyShaderModule(mDevice, shaderModule, nullptr));

        VkPipelineShaderStageCreateInfo vertexShaderStageInfo{};
        vertexShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertexShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertexShaderStageInfo.module = shaderModule;
        vertexShaderStageInfo.pName = "vertexMain";

        VkPipelineShaderStageCreateInfo fragmentShaderStageInfo{};
        fragmentShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragmentShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragmentShaderStageInfo.module = shaderModule;
        fragmentShaderStageInfo.pName = "fragmentMain";

        const VkPipelineShaderStageCreateInfo shaderStages[]
            = {vertexShaderStageInfo, fragmentShaderStageInfo};

        VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
        colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendInfo.attachmentCount = 1;
        colorBlendInfo.pAttachments = &blendAttachmentState;

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
        depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineViewportStateCreateInfo viewportInfo{};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.viewportCount = 1;
        viewportInfo.scissorCount = 1;

        VkPipelineMultisampleStateCreateInfo multisampleInfo{};
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        constexpr VkDynamicState dynamicStates[]
            = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamicInfo{};
        dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicInfo.dynamicStateCount = ARRAY_SIZE(dynamicStates);
        dynamicInfo.pDynamicStates = dynamicStates;

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        renderingInfo.depthAttachmentFormat = depthFormat;
        renderingInfo.stencilAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = ARRAY_SIZE(shaderStages);
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineInfo.pViewportState = &viewportInfo;
        pipelineInfo.pRasterizationState = &rasterizationInfo;
        pipelineInfo.pMultisampleState = &multisampleInfo;
        pipelineInfo.pDepthStencilState = &depthStencilInfo;
        pipelineInfo.pColorBlendState = &colorBlendInfo;
        pipelineInfo.pDynamicState = &dynamicInfo;
        pipelineInfo.layout = mPipelineLayout;
        VK_CHECK(vkCreateGraphicsPipelines(
            mDevice,
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            nullptr,
            &mPipeline
        ));
    }

    return true;
}

void ImGuiRenderer::Cleanup()
{
    vkDeviceWaitIdle(mDevice);

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        Frame& frame = mFrame[i];
        if (frame.mVertexBuffer.mMapped)
        {
            vkUnmapMemory(mDevice, frame.mVertexBuffer.mMemory);
            frame.mVertexBuffer.mMapped = nullptr;
        }
        vkFreeMemory(mDevice, frame.mVertexBuffer.mMemory, nullptr);
        vkDestroyBuffer(mDevice, frame.mVertexBuffer.mBuffer, nullptr);

        if (mFrame[i].mIndexBuffer.mMapped)
        {
            vkUnmapMemory(mDevice, frame.mIndexBuffer.mMemory);
            mFrame[i].mIndexBuffer.mMapped = nullptr;
        }
        vkFreeMemory(mDevice, frame.mIndexBuffer.mMemory, nullptr);
        vkDestroyBuffer(mDevice, frame.mIndexBuffer.mBuffer, nullptr);
    }
    vkDestroyPipeline(mDevice, mPipeline, nullptr);
    vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
    vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);
    vkDestroySampler(mDevice, mFontImage.mSampler, nullptr);
    vkDestroyImageView(mDevice, mFontImage.mView, nullptr);
    vkFreeMemory(mDevice, mFontImage.mMemory, nullptr);
    vkDestroyImage(mDevice, mFontImage.mImage, nullptr);
}

bool ImGuiRenderer::UpdateVertexIndexBuffers(u32 frameIndex)
{
    ImGui::Render();

    const ImDrawData* const drawData = ImGui::GetDrawData();
    if (!drawData)
    {
        return true;
    }

    VkDeviceSize vertexBufferSize
        = static_cast<VkDeviceSize>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    VkDeviceSize indexBufferSize
        = static_cast<VkDeviceSize>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);

    if (vertexBufferSize == 0 || indexBufferSize == 0)
    {
        return true;
    }

    Frame& frame = mFrame[frameIndex];

    // Round buffers up with multiple of a chunk size to minimize the need to recreate them.
    constexpr VkDeviceSize chunkSize = 16384;
    vertexBufferSize = ((vertexBufferSize + chunkSize - 1) / chunkSize) * chunkSize;
    indexBufferSize = ((indexBufferSize + chunkSize - 1) / chunkSize) * chunkSize;

    const bool shouldRecreateVertexBuffer = (frame.mVertexBuffer.mBuffer == VK_NULL_HANDLE)
        || (frame.mVertexBufferSize < vertexBufferSize);
    if (shouldRecreateVertexBuffer)
    {
        if (frame.mVertexBuffer.mMapped)
        {
            vkUnmapMemory(mDevice, frame.mVertexBuffer.mMemory);
            frame.mVertexBuffer.mMapped = nullptr;
        }
        vkFreeMemory(mDevice, frame.mVertexBuffer.mMemory, nullptr);
        vkDestroyBuffer(mDevice, frame.mVertexBuffer.mBuffer, nullptr);
        const bool result = Vulkan::CreateBuffer(
            frame.mVertexBuffer.mBuffer,
            frame.mVertexBuffer.mMemory,
            mPhysicalDevice,
            mDevice,
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        );
        if (!result)
        {
            return false;
        }
        frame.mVertexCount = drawData->TotalVtxCount;
        VK_CHECK(vkMapMemory(
            mDevice,
            frame.mVertexBuffer.mMemory,
            0,
            VK_WHOLE_SIZE,
            0,
            &frame.mVertexBuffer.mMapped
        ));
    }

    const bool shouldRecreateIndexBuffer = (frame.mIndexBuffer.mBuffer == VK_NULL_HANDLE)
        || (frame.mIndexBufferSize < indexBufferSize);
    if (shouldRecreateIndexBuffer)
    {
        if (frame.mIndexBuffer.mMapped)
        {
            vkUnmapMemory(mDevice, frame.mIndexBuffer.mMemory);
            frame.mIndexBuffer.mMapped = nullptr;
        }
        vkFreeMemory(mDevice, frame.mIndexBuffer.mMemory, nullptr);
        vkDestroyBuffer(mDevice, frame.mIndexBuffer.mBuffer, nullptr);
        const bool result = Vulkan::CreateBuffer(
            frame.mIndexBuffer.mBuffer,
            frame.mIndexBuffer.mMemory,
            mPhysicalDevice,
            mDevice,
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        );
        if (!result)
        {
            return false;
        }
        frame.mIndexCount = drawData->TotalIdxCount;
        VK_CHECK(vkMapMemory(
            mDevice,
            frame.mIndexBuffer.mMemory,
            0,
            VK_WHOLE_SIZE,
            0,
            &frame.mIndexBuffer.mMapped
        ));
    }

    // Upload data.
    ImDrawVert* vertexDst = static_cast<ImDrawVert*>(frame.mVertexBuffer.mMapped);
    ImDrawIdx* indexDst = static_cast<ImDrawIdx*>(frame.mIndexBuffer.mMapped);
    for (int i = 0; i < drawData->CmdListsCount; ++i)
    {
        const ImDrawList* const cmdList = drawData->CmdLists[i];
        memcpy(
            vertexDst,
            cmdList->VtxBuffer.Data,
            static_cast<VkDeviceSize>(cmdList->VtxBuffer.Size) * sizeof(ImDrawVert)
        );
        memcpy(
            indexDst,
            cmdList->IdxBuffer.Data,
            static_cast<VkDeviceSize>(cmdList->IdxBuffer.Size) * sizeof(ImDrawIdx)
        );
        vertexDst += cmdList->VtxBuffer.Size;
        indexDst += cmdList->IdxBuffer.Size;
    }
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = frame.mVertexBuffer.mMemory;
    range.size = VK_WHOLE_SIZE;
    VK_CHECK(vkFlushMappedMemoryRanges(mDevice, 1, &range));
    range.memory = frame.mIndexBuffer.mMemory;
    VK_CHECK(vkFlushMappedMemoryRanges(mDevice, 1, &range));

    return true;
}

void ImGuiRenderer::StartNewFrame() const
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

bool ImGuiRenderer::Render(VkCommandBuffer cmd, u32 frameIndex)
{
    const ImDrawData* const drawData = ImGui::GetDrawData();
    i32 vertexOffset = 0;
    u32 indexOffset = 0;

    if (!drawData || drawData->CmdListsCount == 0)
    {
        return true;
    }

    Frame& frame = mFrame[frameIndex];

    if (!frame.mVertexBuffer.mBuffer || !frame.mIndexBuffer.mBuffer)
    {
        return true;
    }

    const ImGuiIO& io = ImGui::GetIO();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        mPipelineLayout,
        0,
        1,
        &mDescriptorSet,
        0,
        nullptr
    );

    mPushConstantBlock.mScale = Vec2{2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y};
    mPushConstantBlock.mTranslate = Vec2{-1.0f};
    vkCmdPushConstants(
        cmd,
        mPipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(mPushConstantBlock),
        &mPushConstantBlock
    );

    VkDeviceSize offsets[1]{};
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame.mVertexBuffer.mBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, frame.mIndexBuffer.mBuffer, 0, VK_INDEX_TYPE_UINT16);

    VkViewport viewport{};
    viewport.width = io.DisplaySize.x;
    viewport.height = io.DisplaySize.y;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    for (int i = 0; i < drawData->CmdListsCount; ++i)
    {
        const ImDrawList* const cmdList = drawData->CmdLists[i];
        for (int j = 0; j < cmdList->CmdBuffer.Size; ++j)
        {
            const ImDrawCmd& imCmd = cmdList->CmdBuffer[j];
            const ImVec4 rect = imCmd.ClipRect;

            VkRect2D scissorRect{};
            scissorRect.offset
                = {Max(static_cast<i32>(rect.x), 0), Max(static_cast<i32>(rect.y), 0)};
            scissorRect.extent
                = {static_cast<u32>(rect.z - rect.x), static_cast<u32>(rect.w - rect.y)};
            vkCmdSetScissor(cmd, 0, 1, &scissorRect);

            vkCmdDrawIndexed(cmd, imCmd.ElemCount, 1, indexOffset, vertexOffset, 0);
            indexOffset += imCmd.ElemCount;
        }
        vertexOffset += cmdList->VtxBuffer.Size;
    }

    return true;
}
