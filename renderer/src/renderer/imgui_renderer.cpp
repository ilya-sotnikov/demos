#include "imgui_renderer.hpp"

#include "math/utils.hpp"
#include "utils.hpp"

#include <SDL3/SDL_video.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <stdio.h>

// Heavily based on Sascha Willems's vulkan examples:
// https://github.com/SaschaWillems/Vulkan/blob/master/base/VulkanUIOverlay.h

bool ImguiRenderer::init(
    SDL_Window* window,
    rhi::Format color_format,
    rhi::Queue queue,
    rhi::CommandPool command_pool
)
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

    uchar* font_data{};
    int tex_width = 0;
    int tex_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_data, &tex_width, &tex_height);
    const u64 upload_size = u64(tex_width * tex_height * 4) * sizeof(char);

    ImGui::StyleColorsDark();

    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (display == 0)
    {
        fprintf(stderr, "SDL_GetPrimaryDisplay failed: %s\n", SDL_GetError());
        return false;
    }
    const f32 window_scale = SDL_GetDisplayContentScale(display);
    if (window_scale == 0.0f)
    {
        fprintf(stderr, "SDL_GetDisplayContentScale failed: %s\n", SDL_GetError());
        return false;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(window_scale);
    style.FontScaleDpi = window_scale;

    m_font_texture = rhi::create_texture({
        .format = rhi::FORMAT_R8G8B8A8_UNORM,
        .dimensions = {u32(tex_width), u32(tex_height), 1},
        .usage = rhi::TEXTURE_USAGE_SAMPLED_BIT | rhi::TEXTURE_USAGE_TRANSFER_DST_BIT,
        .debug_name = "font_texture",
    });

    // Uploading buffer data to font texture.
    {
        rhi::Buffer staging_buffer = rhi::create_buffer({
            .size = upload_size,
            .debug_name = "staging_buffer",
        });
        DEFER(rhi::destroy_buffer(staging_buffer));

        memcpy(rhi::get_buffer_host_ptr(staging_buffer), font_data, upload_size);

        const rhi::CommandBuffer cb = rhi::create_command_buffer({
            .command_pool = command_pool,
            .debug_name = "imgui_font_upload_cb",
        });
        DEFER(rhi::destroy_command_buffer(cb));

        rhi::begin_command_buffer(cb);

        rhi::cmd_texture_barrier(
            cb,
            {{
                m_font_texture,
                rhi::TEXTURE_LAYOUT_UNDEFINED,
                rhi::TEXTURE_LAYOUT_GENERAL,
                rhi::STAGE_HOST_BIT,
                rhi::ACCESS_NONE,
                rhi::STAGE_ALL_TRANSFER_BIT,
                rhi::ACCESS_TRANSFER_WRITE_BIT,
            }}
        );

        rhi::cmd_copy_buffer_to_texture(
            cb,
            staging_buffer,
            m_font_texture,
            {{
                .texture_dimensions = {u32(tex_width), u32(tex_height), 1},
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

        rhi::queue_submit(queue, {{.cb = cb}});

        rhi::queue_wait_idle(queue);
    }

    m_font_sampler = rhi::create_sampler({});

    // Pipeline.
    {
        const utils::FileData vert_data = utils::file_read("imgui.vert.hlsl.spv");
        DEFER(free(vert_data.data));
        const utils::FileData frag_data = utils::file_read("imgui.frag.hlsl.spv");
        DEFER(free(frag_data.data));

        m_pipeline = rhi::create_graphics_pipeline({
            .bytecodes = {
                {static_cast<u8*>(vert_data.data), int(vert_data.size)},
                {static_cast<u8*>(frag_data.data), int(frag_data.size)},
            },
            .stencil_format = rhi::FORMAT_S8_UINT,
            .color_targets = {{
                .format = color_format,
                .blend_enable = true,
                .src_color_factor = rhi::BLEND_FACTOR_SRC_ALPHA,
                .dst_color_factor = rhi::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .src_alpha_factor = rhi::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .dst_alpha_factor = rhi::BLEND_FACTOR_ZERO,
            }},
            .debug_name = "fullscreen_pipeline",
        });
        if (!m_pipeline)
        {
            return false;
        }
    }

    return true;
}

void ImguiRenderer::cleanup()
{
    rhi::device_wait_idle();

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i)
    {
        Frame& frame = m_frame[i];
        rhi::destroy_buffer(frame.vertex_buffer);
        rhi::destroy_buffer(frame.index_buffer);
    }
    rhi::destroy_pipeline(m_pipeline);
    rhi::destroy_sampler(m_font_sampler);
    rhi::destroy_texture(m_font_texture);
}

void ImguiRenderer::update_vertex_index_buffers(u32 frame_index)
{
    ImGui::Render();

    const ImDrawData* const draw_data = ImGui::GetDrawData();
    if (!draw_data)
    {
        return;
    }

    u64 vertex_buffer_size = u64(draw_data->TotalVtxCount) * sizeof(ImDrawVert);
    u64 index_buffer_size = u64(draw_data->TotalIdxCount) * sizeof(ImDrawIdx);

    if (vertex_buffer_size == 0 || index_buffer_size == 0)
    {
        return;
    }

    Frame& frame = m_frame[frame_index];

    // Round buffers up with multiple of a chunk size to minimize the need to recreate them.
    const u64 chunk_size = 16384;
    vertex_buffer_size = ((vertex_buffer_size + chunk_size - 1) / chunk_size) * chunk_size;
    index_buffer_size = ((index_buffer_size + chunk_size - 1) / chunk_size) * chunk_size;

    const bool should_recreate_vertex_buffer
        = !frame.vertex_buffer || (frame.vertex_buffer_size < vertex_buffer_size);
    if (should_recreate_vertex_buffer)
    {
        rhi::destroy_buffer(frame.vertex_buffer);

        frame.vertex_buffer = rhi::create_buffer({
            .size = vertex_buffer_size,
            .debug_name = "imgui_vertex_buffer",
        });
        frame.vertex_count = draw_data->TotalVtxCount;
        frame.vertex_buffer_size = vertex_buffer_size;
    }

    const bool should_recreate_index_buffer
        = !frame.index_buffer || (frame.index_buffer_size < index_buffer_size);
    if (should_recreate_index_buffer)
    {
        rhi::destroy_buffer(frame.index_buffer);

        frame.index_buffer = rhi::create_buffer({
            .size = index_buffer_size,
            .debug_name = "imgui_index_buffer",
        });
        frame.index_count = draw_data->TotalIdxCount;
        frame.index_buffer_size = index_buffer_size;
    }

    // Upload data.
    ImDrawVert* vertex_dst
        = static_cast<ImDrawVert*>(rhi::get_buffer_host_ptr(frame.vertex_buffer));
    ImDrawIdx* index_dst = static_cast<ImDrawIdx*>(rhi::get_buffer_host_ptr(frame.index_buffer));
    for (int i = 0; i < draw_data->CmdListsCount; ++i)
    {
        const ImDrawList* const cmd_list = draw_data->CmdLists[i];
        memcpy(
            vertex_dst,
            cmd_list->VtxBuffer.Data,
            u64(cmd_list->VtxBuffer.Size) * sizeof(ImDrawVert)
        );
        memcpy(
            index_dst,
            cmd_list->IdxBuffer.Data,
            u64(cmd_list->IdxBuffer.Size) * sizeof(ImDrawIdx)
        );
        vertex_dst += cmd_list->VtxBuffer.Size;
        index_dst += cmd_list->IdxBuffer.Size;
    }
}

void ImguiRenderer::start_new_frame() const
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImguiRenderer::render(rhi::CommandBuffer cb, u32 frame_index)
{
    const ImDrawData* const draw_data = ImGui::GetDrawData();
    i32 vertex_offset = 0;
    u32 index_offset = 0;

    if (!draw_data || draw_data->CmdListsCount == 0)
    {
        return;
    }

    Frame& frame = m_frame[frame_index];

    if (!frame.vertex_buffer || !frame.index_buffer)
    {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();

    rhi::cmd_bind_pipeline(cb, m_pipeline);

    rhi::cmd_push_descriptors(
        cb,
        m_pipeline,
        {
            frame.vertex_buffer,
            m_font_texture,
            m_font_sampler,
        }
    );

    const PushConstantsImgui push_constants = {
        .scale = Vec2{2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y},
        .translate = Vec2{-1.0f},
    };
    rhi::cmd_push_constants(cb, m_pipeline, &push_constants);

    rhi::cmd_bind_index_buffer(cb, frame.index_buffer, 0, rhi::INDEX_TYPE_U16);

    rhi::cmd_set_viewport({
        .cb = cb,
        .width = io.DisplaySize.x,
        .height = io.DisplaySize.y,
    });

    for (int i = 0; i < draw_data->CmdListsCount; ++i)
    {
        const ImDrawList* const cmd_list = draw_data->CmdLists[i];
        for (int j = 0; j < cmd_list->CmdBuffer.Size; ++j)
        {
            const ImDrawCmd& im_cmd = cmd_list->CmdBuffer[j];
            const ImVec4 rect = im_cmd.ClipRect;

            rhi::cmd_set_scissor({
                .cb = cb,
                .offset = {Max(i32(rect.x), 0), Max(i32(rect.y), 0)},
                .extent = {u32(rect.z - rect.x), u32(rect.w - rect.y)},
            });

            rhi::cmd_draw_indexed(cb, im_cmd.ElemCount, 1, index_offset, vertex_offset, 0);
            index_offset += im_cmd.ElemCount;
        }
        vertex_offset += cmd_list->VtxBuffer.Size;
    }
}
