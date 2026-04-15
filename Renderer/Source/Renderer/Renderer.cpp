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

    // Visibility buffer pipeline.
    {
        const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        };

        const VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        const VkPipelineViewportStateCreateInfo viewportInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        const VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        };

        const VkPipelineMultisampleStateCreateInfo multisampleInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        const VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_GREATER,
        };

        const VkPipelineColorBlendAttachmentState colorBlendAttachments[] = {
            {
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            },
        };

        const VkPipelineColorBlendStateCreateInfo colorBlending = {
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = ARRAY_SIZE(colorBlendAttachments),
            .pAttachments = colorBlendAttachments,
        };

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        const VkPipelineDynamicStateCreateInfo dynamicState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = u32(ARRAY_SIZE(dynamicStates)),
            .pDynamicStates = dynamicStates,
        };

        const VkFormat colorAttachmentFormats[] = {mVisibilityImage.format};

        const VkPipelineRenderingCreateInfo pipelineRenderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = ARRAY_SIZE(colorAttachmentFormats),
            .pColorAttachmentFormats = colorAttachmentFormats,
            .depthAttachmentFormat = mDepthImage.format,
        };

        static_assert(ARRAY_SIZE(colorBlendAttachments) == ARRAY_SIZE(colorAttachmentFormats));

        VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &pipelineRenderingInfo,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pViewportState = &viewportInfo,
            .pRasterizationState = &rasterizationInfo,
            .pMultisampleState = &multisampleInfo,
            .pDepthStencilState = &depthStencilInfo,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
        };

        if (!mDevice.CreateGraphicsPipeline({
                .pipeline = mVisibilityPipeline,
                .shaderPaths = {"VisibilityBuffer.vert.hlsl.spv", "VisibilityBuffer.frag.hlsl.spv"},
                .pipelineInfo = pipelineInfo,
                .debugName = "VisibilityBufferPass",
            }))
        {
            return false;
        }
    }

    // Debug draw rect pipeline.
    {
        const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        };

        const VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        const VkPipelineViewportStateCreateInfo viewportInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        const VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .lineWidth = 1.0f,
        };

        const VkPipelineMultisampleStateCreateInfo multisampleInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        const VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        };

        const VkPipelineColorBlendAttachmentState colorBlendAttachments[] = {
            {
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            },
        };

        const VkPipelineColorBlendStateCreateInfo colorBlending = {
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = ARRAY_SIZE(colorBlendAttachments),
            .pAttachments = colorBlendAttachments,
        };

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        const VkPipelineDynamicStateCreateInfo dynamicState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = u32(ARRAY_SIZE(dynamicStates)),
            .pDynamicStates = dynamicStates,
        };

        const VkFormat colorAttachmentFormats[] = {mRenderImage.format};

        const VkPipelineRenderingCreateInfo pipelineRenderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = ARRAY_SIZE(colorAttachmentFormats),
            .pColorAttachmentFormats = colorAttachmentFormats,
            .depthAttachmentFormat = mDepthImage.format,
        };

        static_assert(ARRAY_SIZE(colorBlendAttachments) == ARRAY_SIZE(colorAttachmentFormats));

        VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &pipelineRenderingInfo,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pViewportState = &viewportInfo,
            .pRasterizationState = &rasterizationInfo,
            .pMultisampleState = &multisampleInfo,
            .pDepthStencilState = &depthStencilInfo,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
        };

        if (!mDevice.CreateGraphicsPipeline({
                .pipeline = mDebugDrawRectPipeline,
                .shaderPaths = {"DebugDrawRect.vert.hlsl.spv", "DebugDrawRect.frag.hlsl.spv"},
                .pipelineInfo = pipelineInfo,
                .debugName = "DebugDrawRectPass",
            }))
        {
            return false;
        }
    }

    // Fullscreen triangle pipeline.
    {
        const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        };

        const VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        const VkPipelineViewportStateCreateInfo viewportInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        const VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .cullMode = VK_CULL_MODE_NONE,
            .lineWidth = 1.0f,
        };

        const VkPipelineMultisampleStateCreateInfo multisampleInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        const VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        };

        const VkPipelineColorBlendAttachmentState colorBlendAttachments[] = {
            {
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            },
        };

        const VkPipelineColorBlendStateCreateInfo colorBlending = {
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = ARRAY_SIZE(colorBlendAttachments),
            .pAttachments = colorBlendAttachments,
        };

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        const VkPipelineDynamicStateCreateInfo dynamicState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = u32(ARRAY_SIZE(dynamicStates)),
            .pDynamicStates = dynamicStates,
        };

        const VkFormat colorAttachmentFormats[] = {mSwapchain.surfaceFormat.format};

        const VkPipelineRenderingCreateInfo pipelineRenderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = ARRAY_SIZE(colorAttachmentFormats),
            .pColorAttachmentFormats = colorAttachmentFormats,
        };

        static_assert(ARRAY_SIZE(colorBlendAttachments) == ARRAY_SIZE(colorAttachmentFormats));

        VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &pipelineRenderingInfo,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pViewportState = &viewportInfo,
            .pRasterizationState = &rasterizationInfo,
            .pMultisampleState = &multisampleInfo,
            .pDepthStencilState = &depthStencilInfo,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
        };

        if (!mDevice.CreateGraphicsPipeline({
                .pipeline = mFullscreenPipeline,
                .shaderPaths = {"Fullscreen.vert.hlsl.spv", "Fullscreen.frag.hlsl.spv"},
                .pipelineInfo = pipelineInfo,
                .debugName = "FullscreenPass",
            }))
        {
            return false;
        }
    }

    // Debug grad error pipeline.
    {
        const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        };

        const VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        const VkPipelineViewportStateCreateInfo viewportInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        const VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        };

        const VkPipelineMultisampleStateCreateInfo multisampleInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        const VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_GREATER,
        };

        const VkPipelineColorBlendAttachmentState colorBlendAttachments[] = {
            {
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            },
        };

        const VkPipelineColorBlendStateCreateInfo colorBlending = {
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = ARRAY_SIZE(colorBlendAttachments),
            .pAttachments = colorBlendAttachments,
        };

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        const VkPipelineDynamicStateCreateInfo dynamicState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = u32(ARRAY_SIZE(dynamicStates)),
            .pDynamicStates = dynamicStates,
        };

        const VkFormat colorAttachmentFormats[] = {mSwapchain.surfaceFormat.format};

        const VkPipelineRenderingCreateInfo pipelineRenderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = ARRAY_SIZE(colorAttachmentFormats),
            .pColorAttachmentFormats = colorAttachmentFormats,
            .depthAttachmentFormat = mDepthImage.format,
        };

        static_assert(ARRAY_SIZE(colorBlendAttachments) == ARRAY_SIZE(colorAttachmentFormats));

        VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &pipelineRenderingInfo,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pViewportState = &viewportInfo,
            .pRasterizationState = &rasterizationInfo,
            .pMultisampleState = &multisampleInfo,
            .pDepthStencilState = &depthStencilInfo,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
        };

        if (!mDevice.CreateGraphicsPipeline({
                .pipeline = mDebugGradErrorPipeline,
                .shaderPaths = {"DebugGradError.vert.hlsl.spv", "DebugGradError.frag.hlsl.spv"},
                .pipelineInfo = pipelineInfo,
                .debugName = "DebugGradErrorPass",
            }))
        {
            return false;
        }
    }

    // Compute pipelines.
    {
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
                .pipeline = mRenderPipeline,
                .shaderPath = "Renderer.comp.hlsl.spv",
                .extraDescriptorSetLayout = mTextureDescriptorSetLayout,
                .debugName = "RenderPass",
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
                .pipeline = mDebugDrawFillCmdPipeline,
                .shaderPath = "DebugDrawFillCmd.comp.hlsl.spv",
                .debugName = "DebugDrawFillCmdPass",
            }))
        {
            return false;
        }
    }

    // Command pool.
    {
        const VkCommandPoolCreateInfo cmdPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = mDevice.mQueueInfo.familyIdx,
        };

        VK_CHECK(vkCreateCommandPool(mDevice.mDevice, &cmdPoolInfo, nullptr, &mCommandPool));
    }

    // Command buffers.
    {
        const VkCommandBufferAllocateInfo cmdBufferAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = mCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VK_CHECK(vkAllocateCommandBuffers(
                mDevice.mDevice,
                &cmdBufferAllocateInfo,
                &mFrame[i].commandBuffer
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
                vkCreateFence(mDevice.mDevice, &fenceInfo, nullptr, &mFrame[i].queueSubmitFence)
            );
            VK_CHECK(vkCreateSemaphore(
                mDevice.mDevice,
                &semaphoreInfo,
                nullptr,
                &mFrame[i].imageAcquireSemaphore
            ));
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

        // TODO: would be nice if something like VK_NV_cluster_acceleration_structure
        // would become core eventually.
        if (!CreateAndUploadBlas(meshPrimitives, drawCmds))
        {
            printf("vulkan: BLAS creating and uploading failed\n");
            return false;
        }

        if (!CreateAndUploadTlas(meshPrimitives, drawData))
        {
            printf("vulkan: TLAS creating and uploading failed\n");
            return false;
        }
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

    if (!mImguiRenderer.Init(mWindow, mDevice, mCommandPool, mSwapchain.surfaceFormat.format))
    {
        fprintf(stderr, "Failed to initialize ImGui renderer\n");
        return false;
    }

    // Initializing resources.
    {
        const VkCommandBuffer cmd = mFrame[0].commandBuffer;

        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

        vkCmdFillBuffer(
            cmd,
            mMeshPrimitiveVisibleBuffer.buffer,
            0,
            sizeof(u32) * MAX_DRAW_CALLS,
            0
        );

        VK_CHECK(vkEndCommandBuffer(cmd));

        const VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
        };

        VK_CHECK(vkQueueSubmit(mDevice.mQueueInfo.queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(mDevice.mQueueInfo.queue));
    }

    mSwapchainNeedsRecreating = true;
    mTaaJitterMaxIdx = 8;
    mUniformData.taaBlendWeight = 0.1f;
    mUniformData.ambientIntensity = 0.1f;
    mUniformData.sunIntensity = 5.0f;
    mUniformData.gradErrorMax = 0.01f;

    return true;
}

void Renderer::Cleanup()
{
    if (mDevice.mDevice)
    {
        return;
    }

    (void)vkDeviceWaitIdle(mDevice.mDevice);

    mImguiRenderer.Cleanup();

    CleanupColorResources();
    CleanupDepthResources();

    for (Vulkan::Image& tex : mTextures)
    {
        mDevice.DestroyImage(tex);
    }

    for (VkAccelerationStructureKHR as : mBlas)
    {
        vkDestroyAccelerationStructureKHR(mDevice.mDevice, as, nullptr);
    }
    vkDestroyAccelerationStructureKHR(mDevice.mDevice, mTlas, nullptr);
    mDevice.DestroyBuffer(mDebugDrawCmdBuffer);
    mDevice.DestroyBuffer(mDebugDrawRectBuffer);
    mDevice.DestroyBuffer(mDebugDrawCountBuffer);
    mDevice.DestroyBuffer(mMeshPrimitiveVisibleBuffer);
    mDevice.DestroyBuffer(mTlasBuffer);
    mDevice.DestroyBuffer(mBlasBuffer);
    mDevice.DestroyBuffer(mDrawCountBuffer);
    mDevice.DestroyBuffer(mMaterialBuffer);
    mDevice.DestroyBuffer(mDrawDataBuffer);
    mDevice.DestroyBuffer(mVertexBuffer);
    mDevice.DestroyBuffer(mIndexBuffer);
    mDevice.DestroyBuffer(mDrawIndicesEarlyBuffer);
    mDevice.DestroyBuffer(mDrawIndicesLateBuffer);
    mDevice.DestroyBuffer(mDrawCmdEarlyBuffer2);
    mDevice.DestroyBuffer(mDrawCmdLateBuffer2);
    mDevice.DestroyBuffer(mDrawCmdBuffer1);
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        mDevice.DestroyBuffer(mFrame[i].uniformBuffer);
    }
    for (int i = 0; i < RENDERER_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroyFence(mDevice.mDevice, mFrame[i].queueSubmitFence, nullptr);
        vkDestroySemaphore(mDevice.mDevice, mFrame[i].imageAcquireSemaphore, nullptr);
    }
    for (VkSemaphore sem : mRenderFinishedSemaphores)
    {
        vkDestroySemaphore(mDevice.mDevice, sem, nullptr);
    }
    vkDestroyDescriptorPool(mDevice.mDevice, mDescriptorPool, nullptr);
    vkDestroySampler(mDevice.mDevice, mMinSampler, nullptr);
    vkDestroySampler(mDevice.mDevice, mLinearSampler, nullptr);
    vkDestroySampler(mDevice.mDevice, mTextureSampler, nullptr);
    vkDestroyCommandPool(mDevice.mDevice, mCommandPool, nullptr);
    vkDestroyDescriptorSetLayout(mDevice.mDevice, mTextureDescriptorSetLayout, nullptr);
    mDevice.DestroyPipeline(mDebugDrawFillCmdPipeline);
    mDevice.DestroyPipeline(mDebugDrawRectPipeline);
    mDevice.DestroyPipeline(mDepthReducePipeline);
    mDevice.DestroyPipeline(mDebugGradErrorPipeline);
    mDevice.DestroyPipeline(mTaaResolvePipeline);
    mDevice.DestroyPipeline(mCullLatePipeline);
    mDevice.DestroyPipeline(mCullEarlyPipeline);
    mDevice.DestroyPipeline(mFullscreenPipeline);
    mDevice.DestroyPipeline(mRenderPipeline);
    mDevice.DestroyPipeline(mVisibilityPipeline);
    CleanupSwapchain();
    vkDestroySurfaceKHR(mDevice.mInstance, mSurface, nullptr);
    mDevice.Destroy();
    volkFinalize();
}

bool Renderer::StartNewFrame()
{
    DEBUG_ASSERT(!mNewFrameStarted);

    Frame& frame = mFrame[mFrameIdx];

    VK_CHECK(vkWaitForFences(mDevice.mDevice, 1, &frame.queueSubmitFence, VK_TRUE, 1'000'000'000));
    VK_CHECK(vkResetFences(mDevice.mDevice, 1, &frame.queueSubmitFence));

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

    u32 imageIdx = 0;
    VkResult vulkanResult = vkAcquireNextImageKHR(
        mDevice.mDevice,
        mSwapchain.swapchain,
        1'000'000'000,
        frame.imageAcquireSemaphore,
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

    if ((mUniformData.taaEnable == 1) && (mRenderMode == RenderMode::Normal))
    {
        const f32 haltonX = 2.0f * HaltonSequence(mTaaJitterIdx + 1, 2) - 1.0f;
        const f32 haltonY = 2.0f * HaltonSequence(mTaaJitterIdx + 1, 3) - 1.0f;
        mUniformData.prevTaaJitter = mUniformData.taaJitter;
        mUniformData.taaJitter = {
            haltonX / (f32(mUniformData.renderWidth) * 2.0f),
            haltonY / (f32(mUniformData.renderHeight) * 2.0f),
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

    mUniformData.sunDirectionView
        = (mUniformData.worldToView * Vec4{mUniformData.sunDirectionWorld, 0.0f}).XYZ();
    mUniformData.sunDirectionView = Normalize(mUniformData.sunDirectionView);

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

    memcpy(frame.uniformBuffer.mapped, &mUniformData, sizeof(mUniformData));

    switch (mRenderMode)
    {
    case RenderMode::Normal:
        if (!RecordCommandBuffer(imageIdx))
        {
            return false;
        }
        break;
    case RenderMode::GradError:
        if (!RecordDebugGradErrorCommandBuffer(imageIdx))
        {
            return false;
        }
        break;
    }

    const VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.imageAcquireSemaphore,
        .pWaitDstStageMask = &waitDstStageMask,
        .commandBufferCount = 1,
        .pCommandBuffers = &frame.commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &mRenderFinishedSemaphores[imageIdx],
    };
    VK_CHECK(vkQueueSubmit(mDevice.mQueueInfo.queue, 1, &submitInfo, frame.queueSubmitFence));

    const VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &mRenderFinishedSemaphores[imageIdx],
        .swapchainCount = 1,
        .pSwapchains = &mSwapchain.swapchain,
        .pImageIndices = &imageIdx,
    };
    vulkanResult = vkQueuePresentKHR(mDevice.mQueueInfo.queue, &presentInfo);
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
    if (mRenderMode != mode)
    {
        mRenderModeChanged = true;
        mRenderMode = mode;
    }
}

void Renderer::FreezeCullCamera(bool frozen)
{
    mCullCameraFrozen = frozen;
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

        const VkCommandBuffer cmd = mFrame[0].commandBuffer;

        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

        Vulkan::CmdImageMemoryBarrier(
            cmd,
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
            cmd,
            stagingBuffer.buffer,
            mTextures[i].image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            u32(copyRegions.size()),
            copyRegions.data()
        );

        Vulkan::CmdImageMemoryBarrier(
            cmd,
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

        VK_CHECK(vkEndCommandBuffer(cmd));

        const VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
        };

        VK_CHECK(vkQueueSubmit(mDevice.mQueueInfo.queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(mDevice.mQueueInfo.queue));
    }

    return true;
}

bool Renderer::CreateAndUploadBlas(
    const std::vector<MeshPrimitive>& meshPrimitives,
    const std::vector<VkDrawIndexedIndirectCommand>& drawCmds
)
{
    DEBUG_ASSERT(!meshPrimitives.empty());
    DEBUG_ASSERT(!drawCmds.empty());
    DEBUG_ASSERT(meshPrimitives.size() == drawCmds.size());

    std::vector<VkAccelerationStructureGeometryKHR> geometries(meshPrimitives.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos(meshPrimitives.size());
    std::vector<u32> primitiveCounts(meshPrimitives.size());
    std::vector<size_t> accelerationOffsets(meshPrimitives.size());
    std::vector<size_t> accelerationSizes(meshPrimitives.size());
    std::vector<size_t> scratchOffsets(meshPrimitives.size());

    const size_t ALIGNMENT = 256;

    size_t totalAccelerationSize = 0;
    size_t totalPrimitiveCount = 0;
    size_t totalScratchSize = 0;

    for (size_t i = 0; i < meshPrimitives.size(); ++i)
    {
        const MeshPrimitive& primitive = meshPrimitives[i];
        const VkDrawIndexedIndirectCommand& drawCmd = drawCmds[i];
        VkAccelerationStructureGeometryKHR& geometry = geometries[i];
        VkAccelerationStructureBuildGeometryInfoKHR& buildInfo = buildInfos[i];

        ASSERT(primitive.vertexCount > 0);

        primitiveCounts[i] = drawCmd.indexCount / 3;

        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles.sType
            = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        geometry.geometry.triangles.vertexData.deviceAddress
            = mVertexBuffer.deviceAddress + size_t(drawCmd.vertexOffset) * sizeof(Vertex);
        geometry.geometry.triangles.vertexStride = sizeof(Vertex);
        geometry.geometry.triangles.maxVertex = u32(primitive.vertexCount - 1);
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geometry.geometry.triangles.indexData.deviceAddress
            = mIndexBuffer.deviceAddress + drawCmd.firstIndex * sizeof(u32);
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(
            mDevice.mDevice,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo,
            &primitiveCounts[i],
            &sizeInfo
        );

        accelerationOffsets[i] = totalAccelerationSize;
        accelerationSizes[i] = sizeInfo.accelerationStructureSize;
        scratchOffsets[i] = totalScratchSize;

        totalAccelerationSize = Utils::AlignUpPow2(
            totalAccelerationSize + sizeInfo.accelerationStructureSize,
            ALIGNMENT
        );
        totalScratchSize
            = Utils::AlignUpPow2(totalScratchSize + sizeInfo.buildScratchSize, ALIGNMENT);
        totalPrimitiveCount += primitiveCounts[i];
    }

    if (!mDevice.CreateBuffer({
            .buffer = mBlasBuffer,
            .size = totalAccelerationSize,
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .debugName = "BlasBuffer",
        }))
    {
        return false;
    }

    Vulkan::Buffer scratchBuffer{};
    if (!mDevice.CreateBuffer({
            .buffer = scratchBuffer,
            .size = totalScratchSize,
            .minAlignment = ALIGNMENT,
            .debugName = "ScratchBuffer",
        }))
    {
        return false;
    }
    DEFER(mDevice.DestroyBuffer(scratchBuffer));

    printf(
        "BLAS accelerationStructureSize: %.2f MB, scratchSize: %.2f MB, %.3fM "
        "triangles\n",
        f64(totalAccelerationSize) / 1.0e6,
        f64(totalScratchSize) / 1.0e6,
        f64(totalPrimitiveCount) / 1.0e6
    );

    mBlas.resize(meshPrimitives.size());

    for (size_t i = 0; i < meshPrimitives.size(); ++i)
    {
        const VkAccelerationStructureCreateInfoKHR accelerationInfo = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = mBlasBuffer.buffer,
            .offset = accelerationOffsets[i],
            .size = accelerationSizes[i],
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        };
        VK_CHECK(
            vkCreateAccelerationStructureKHR(mDevice.mDevice, &accelerationInfo, nullptr, &mBlas[i])
        );
    }

    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges(meshPrimitives.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> buildRangePtrs(
        meshPrimitives.size()
    );

    for (size_t i = 0; i < meshPrimitives.size(); ++i)
    {
        buildInfos[i].dstAccelerationStructure = mBlas[i];
        buildInfos[i].scratchData.deviceAddress = scratchBuffer.deviceAddress + scratchOffsets[i];

        buildRanges[i].primitiveCount = primitiveCounts[i];
        buildRangePtrs[i] = &buildRanges[i];
    }

    const VkCommandBuffer cmd = mFrame[0].commandBuffer;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    vkCmdBuildAccelerationStructuresKHR(
        cmd,
        u32(buildInfos.size()),
        buildInfos.data(),
        buildRangePtrs.data()
    );

    VK_CHECK(vkEndCommandBuffer(cmd));

    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };

    VK_CHECK(vkQueueSubmit(mDevice.mQueueInfo.queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkDeviceWaitIdle(mDevice.mDevice));

    return true;
}

bool Renderer::CreateAndUploadTlas(
    const std::vector<MeshPrimitive>& meshPrimitives,
    const std::vector<DrawData>& drawData
)
{
    DEBUG_ASSERT(!meshPrimitives.empty());
    DEBUG_ASSERT(!drawData.empty());

    const size_t ALIGNMENT = 256;
    const VkCommandBuffer cmd = mFrame[0].commandBuffer;

    Vulkan::Buffer instances{};
    if (!mDevice.CreateBuffer({
            .buffer = instances,
            .size = sizeof(VkAccelerationStructureInstanceKHR) * meshPrimitives.size(),
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .requiredFlags
            = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            .debugName = "InstanceBuffer",
        }))
    {
        return false;
    }
    DEFER(mDevice.DestroyBuffer(instances));

    std::vector<VkDeviceAddress> blasAddresses(mBlas.size());

    for (size_t i = 0; i < mBlas.size(); ++i)
    {
        const VkAccelerationStructureDeviceAddressInfoKHR info = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .accelerationStructure = mBlas[i],
        };
        blasAddresses[i] = vkGetAccelerationStructureDeviceAddressKHR(mDevice.mDevice, &info);
        if (!blasAddresses[i])
        {
            fprintf(stderr, "vkGetAccelerationStructureDeviceAddressKHR failed, idx = %zu\n", i);
            return false;
        }
    }

    for (size_t i = 0; i < drawData.size(); ++i)
    {
        const Mat4 transform
            = Transpose(Model(drawData[i].position, drawData[i].orientation, drawData[i].scale));

        VkAccelerationStructureInstanceKHR instance{};
        memcpy(
            instance.transform.matrix[0],
            &transform.col[0].val[0],
            sizeof(instance.transform.matrix[0])
        );
        memcpy(
            instance.transform.matrix[1],
            &transform.col[1].val[0],
            sizeof(instance.transform.matrix[1])
        );
        memcpy(
            instance.transform.matrix[2],
            &transform.col[2].val[0],
            sizeof(instance.transform.matrix[2])
        );
        instance.instanceCustomIndex = u32(i);
        instance.mask = 0xff;
        instance.accelerationStructureReference = blasAddresses[i];
        instance.flags = drawData[i].renderPassFlags == RENDER_PASS_OPAQUE_BIT
            ? VkGeometryInstanceFlagsKHR(VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR)
            : VkGeometryInstanceFlagsKHR(VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR);

        memcpy(
            static_cast<VkAccelerationStructureInstanceKHR*>(instances.mapped) + i,
            &instance,
            sizeof(VkAccelerationStructureInstanceKHR)
        );
    }

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType
        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = instances.deviceAddress;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    const u32 primitiveCount = u32(meshPrimitives.size());

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    vkGetAccelerationStructureBuildSizesKHR(
        mDevice.mDevice,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo
    );

    printf(
        "TLAS accelerationStructureSize: %.2f MB, scratchSize: %.2f MB\n",
        f64(sizeInfo.accelerationStructureSize) / 1.0e6,
        f64(sizeInfo.buildScratchSize) / 1.0e6
    );

    if (!mDevice.CreateBuffer({
            .buffer = mTlasBuffer,
            .size = sizeInfo.accelerationStructureSize,
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .debugName = "TlasBuffer",
        }))
    {
        return false;
    }

    Vulkan::Buffer scratchBuffer{};
    if (!mDevice.CreateBuffer({
            .buffer = scratchBuffer,
            .size = sizeInfo.buildScratchSize,
            .minAlignment = ALIGNMENT,
            .debugName = "ScratchBuffer",
        }))
    {
        return false;
    }
    DEFER(mDevice.DestroyBuffer(scratchBuffer));

    const VkAccelerationStructureCreateInfoKHR accelerationInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = mTlasBuffer.buffer,
        .size = sizeInfo.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    };

    VK_CHECK(vkCreateAccelerationStructureKHR(mDevice.mDevice, &accelerationInfo, nullptr, &mTlas));

    buildInfo.dstAccelerationStructure = mTlas;
    buildInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

    const VkAccelerationStructureBuildRangeInfoKHR buildRange = {
        .primitiveCount = primitiveCount,
    };
    const VkAccelerationStructureBuildRangeInfoKHR* const buildRangePtr = &buildRange;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    const VkCommandBufferBeginInfo cmdBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &buildRangePtr);

    VK_CHECK(vkEndCommandBuffer(cmd));

    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };

    VK_CHECK(vkQueueSubmit(mDevice.mQueueInfo.queue, 1, &submitInfo, VK_NULL_HANDLE));

    VK_CHECK(vkDeviceWaitIdle(mDevice.mDevice));

    return true;
}

void Renderer::VisibilityBufferPass(VkCommandBuffer cmd, bool cullLate)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mVisibilityPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
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
        cmd,
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
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    const VkRect2D scissor = {
        .extent = mRenderImageExtent,
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

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

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdBindIndexBuffer(cmd, mIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirectCount(
        cmd,
        cullLate ? mDrawCmdLateBuffer2.buffer : mDrawCmdEarlyBuffer2.buffer,
        0,
        mDrawCountBuffer.buffer,
        0,
        mUniformData.drawCount,
        sizeof(VkDrawIndexedIndirectCommand)
    );

    vkCmdEndRendering(cmd);
}

void Renderer::CullPass(VkCommandBuffer cmd, bool late)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        late ? mCullLatePipeline.pipeline : mCullEarlyPipeline.pipeline
    );

    Vulkan::CmdPushDescriptors(
        cmd,
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

    vkCmdDispatch(cmd, GetDispatchSize(mUniformData.drawCount, RENDERER_CULL_WORKGROUP_SIZE), 1, 1);
}

void Renderer::DepthReducePass(VkCommandBuffer cmd)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mDepthReducePipeline.pipeline);

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
            cmd,
            mDepthReducePipeline.layout,
            VK_SHADER_STAGE_ALL,
            0,
            sizeof(pushConstants),
            &pushConstants
        );

        vkCmdPushDescriptorSetWithTemplate(
            cmd,
            mDepthReducePipeline.descriptorUpdateTemplate,
            mDepthReducePipeline.layout,
            0,
            descInfos
        );

        vkCmdDispatch(
            cmd,
            GetDispatchSize(width, RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_X),
            GetDispatchSize(height, RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_Y),
            1
        );

        Vulkan::CmdMemoryBarrier(
            cmd,
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

void Renderer::RenderPass(VkCommandBuffer cmd)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mRenderPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
        mRenderPipeline,
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
            mTlas,
            {mVisibilityImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mVelocityImage.view, VK_IMAGE_LAYOUT_GENERAL},
            {mRenderImage.view, VK_IMAGE_LAYOUT_GENERAL},
        }
    );

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        mRenderPipeline.layout,
        1,
        1,
        &mTextureDescriptorSet,
        0,
        nullptr
    );

    vkCmdDispatch(
        cmd,
        GetDispatchSize(mRenderImageExtent.width, RENDERER_RENDER_WORKGROUP_SIZE_X),
        GetDispatchSize(mRenderImageExtent.height, RENDERER_RENDER_WORKGROUP_SIZE_Y),
        1
    );
}

void Renderer::TaaResolvePass(VkCommandBuffer cmd)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mTaaResolvePipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
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
        cmd,
        GetDispatchSize(mRenderImageExtent.width, RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_X),
        GetDispatchSize(mRenderImageExtent.height, RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_Y),
        1
    );
}

void Renderer::DebugDrawPass(VkCommandBuffer cmd)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mDebugDrawFillCmdPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
        mDebugDrawFillCmdPipeline,
        {
            mDebugDrawCountBuffer.buffer,
            mDebugDrawCmdBuffer.buffer,
        }
    );

    vkCmdDispatch(cmd, 1, 1, 1);

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
            ),
        }
    );

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mDebugDrawRectPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
        mDebugDrawRectPipeline,
        {
            mFrame[mFrameIdx].uniformBuffer.buffer,
            mDebugDrawRectBuffer.buffer,
        }
    );

    const VkViewport viewport = {
        .width = f32(mSwapchain.extent.width),
        .height = f32(mSwapchain.extent.height),
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    const VkRect2D scissor = {
        .extent = mSwapchain.extent,
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

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

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdDrawIndirect(cmd, mDebugDrawCmdBuffer.buffer, 0, 1, 0);

    vkCmdEndRendering(cmd);
}

void Renderer::FullscreenPass(VkCommandBuffer cmd, u32 imageIdx)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mFullscreenPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
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
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    const VkRect2D scissor = {
        .extent = mSwapchain.extent,
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

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

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);
}

void Renderer::DebugDrawGradErrorPass(VkCommandBuffer cmd, bool cullLate, u32 imageIdx)
{
    DEBUG_ASSERT(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mDebugGradErrorPipeline.pipeline);

    Vulkan::CmdPushDescriptors(
        cmd,
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
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    const VkRect2D scissor = {
        .extent = mSwapchain.extent,
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

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

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdBindIndexBuffer(cmd, mIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirectCount(
        cmd,
        cullLate ? mDrawCmdLateBuffer2.buffer : mDrawCmdEarlyBuffer2.buffer,
        0,
        mDrawCountBuffer.buffer,
        0,
        mUniformData.drawCount,
        sizeof(VkDrawIndexedIndirectCommand)
    );

    vkCmdEndRendering(cmd);
}

bool Renderer::RecordDebugGradErrorCommandBuffer(u32 imageIdx)
{
    Frame& frame = mFrame[mFrameIdx];

    const VkCommandBuffer cmd = frame.commandBuffer;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    const VkCommandBufferBeginInfo cmdBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cmd, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdMemoryBarrier(
        cmd,
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

    CullPass(cmd, false);

    Vulkan::CmdBarrier(
        cmd,
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

    DebugDrawGradErrorPass(cmd, false, imageIdx);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
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
        DepthReducePass(cmd);
    }

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cmd, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdBarrier(
        cmd,
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

    CullPass(cmd, true);

    Vulkan::CmdBarrier(
        cmd,
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

    DebugDrawGradErrorPass(cmd, true, imageIdx);

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

        vkCmdBeginRendering(cmd, &renderingInfo);

        if (!mImguiRenderer.Render(cmd, u32(mFrameIdx)))
        {
            return false;
        }

        vkCmdEndRendering(cmd);
    }

    Vulkan::CmdImageMemoryBarrier(
        cmd,
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

    VK_CHECK(vkEndCommandBuffer(cmd));

    return true;
}

bool Renderer::RecordCommandBuffer(u32 imageIdx)
{
    Frame& frame = mFrame[mFrameIdx];

    const VkCommandBuffer cmd = frame.commandBuffer;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    const VkCommandBufferBeginInfo cmdBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cmd, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);
    vkCmdFillBuffer(cmd, mDebugDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdMemoryBarrier(
        cmd,
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

    CullPass(cmd, false);

    Vulkan::CmdBarrier(
        cmd,
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
                mVisibilityImage.image,
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

    VisibilityBufferPass(cmd, false);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
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
        DepthReducePass(cmd);
    }

    Vulkan::CmdMemoryBarrier(
        cmd,
        {
            Vulkan::MemoryBarrier(
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE
            ),
        }
    );

    vkCmdFillBuffer(cmd, mDrawCountBuffer.buffer, 0, sizeof(u32), 0);

    Vulkan::CmdBarrier(
        cmd,
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

    CullPass(cmd, true);

    Vulkan::CmdBarrier(
        cmd,
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

    VisibilityBufferPass(cmd, true);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
        {
            Vulkan::ImageMemoryBarrier(
                mVisibilityImage.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mRenderImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
            Vulkan::ImageMemoryBarrier(
                mVelocityImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            ),
        }
    );

    RenderPass(cmd);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
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
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            ),
        }
    );

    TaaResolvePass(cmd);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
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

    DebugDrawPass(cmd);

    Vulkan::CmdImageMemoryBarrier(
        cmd,
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

    FullscreenPass(cmd, imageIdx);

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

        vkCmdBeginRendering(cmd, &renderingInfo);

        if (!mImguiRenderer.Render(cmd, u32(mFrameIdx)))
        {
            return false;
        }

        vkCmdEndRendering(cmd);
    }

    Vulkan::CmdImageMemoryBarrier(
        cmd,
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

    VK_CHECK(vkEndCommandBuffer(cmd));

    return true;
}

bool Renderer::CreateSwapchain()
{
    VK_CHECK(vkDeviceWaitIdle(mDevice.mDevice));

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

    if (!mDevice.CreateImage({
            .image = mRenderImage,
            .formats = {
                VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
                VK_FORMAT_B10G11R11_UFLOAT_PACK32,
            },
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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
    mDevice.DestroyImage(mVisibilityImage);
    mDevice.DestroyImage(mRenderImage);
    mDevice.DestroyImage(mVelocityImage);
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
    mDevice.DestroyImage(mDepthPyramidImage);
    for (VkImageView& view : mDepthPyramidMipImageViews)
    {
        vkDestroyImageView(mDevice.mDevice, view, nullptr);
        view = VK_NULL_HANDLE;
    }
}
