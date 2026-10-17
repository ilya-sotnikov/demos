#pragma once

#include "common.hpp"
#include "math/types.hpp"
#include "renderer/rhi/rhi.hpp"
#include "renderer/shaders/shared_config.hlsli"
#include "renderer/shaders/shared_def.hlsli"

#include "math/types.hpp"

struct SDL_Window;

struct ImguiRenderer {
    struct PushConstantBlock {
        Vec2 scale;
        Vec2 translate;
    };

    rhi::Texture m_font_texture;
    rhi::Sampler m_font_sampler;
    rhi::Pipeline m_pipeline;

    struct Frame {
        rhi::Buffer vertex_buffer;
        rhi::Buffer index_buffer;
        u64 vertex_buffer_size;
        u64 index_buffer_size;
        int vertex_count;
        int index_count;
    } m_frame[rhi::FRAMES_IN_FLIGHT];

    bool init(
        SDL_Window* window,
        rhi::Format color_format,
        rhi::Queue queue,
        rhi::CommandPool command_pool
    );
    void cleanup();
    void update_vertex_index_buffers(u32 frame_index);
    void start_new_frame() const;
    void render(rhi::CommandBuffer cb, u32 frame_index);
};
