#pragma once

#include "common.hpp"
#include "arena.hpp"
#include "imgui_renderer.hpp"
#include "math/types.hpp"
#include "math/utils.hpp"
#include "rhi/rhi.hpp"
#include "scene.hpp"
#include "shaders/shared_config.hlsli"
#include "shaders/shared_def.hlsli"
#include "uniform_buffer_manager.hpp"

struct SDL_Window;

struct Renderer {
    static constexpr f32 FOV_Y_RAD = math::radians(70.0f);
    static constexpr int MAX_DRAW_CALLS = 4096;
    // NOTE: Can set to 2 to torture my GPU since it's powerful but my monitor is 1080p.
    // TODO: UI toggle?
    static constexpr int RENDER_SCALE = 1;

    static_assert(sizeof(UniformData) <= rhi::UNIFORM_BUFFER_MAX_SIZE_BYTES);

    struct Semaphore {
        rhi::Semaphore semaphore;
        u64 value;

        u64 inc() {
            return ++value;
        }
    };

    struct Frame {
        UniformBufferManager uniform_buffer_manager;
        Semaphore start_semaphore;
        Semaphore shadow_semaphore;
        Semaphore ssao_semaphore;
        rhi::CommandPool graphics_command_pool;
        rhi::CommandPool compute_command_pool;
        rhi::CommandPool transfer_command_pool;
        rhi::CommandBuffer start_command_buffer;
        rhi::CommandBuffer shadow_command_buffer;
        rhi::CommandBuffer ssao_command_buffer;
        rhi::CommandBuffer end_command_buffer;
        rhi::Texture resolved_render_texture;
    };

    Arena m_scratch_arena;
    SDL_Window* m_window;
    rhi::Queue m_graphics_queue;
    rhi::Queue m_compute_queue;
    rhi::Queue m_transfer_queue;
    rhi::DescriptorInfo m_uniform_buffer_descriptor;
    rhi::DescriptorInfo m_shadow_pass_uniform_buffer_descriptor;
    rhi::Semaphore m_frame_semaphore;
    rhi::Texture m_visibility_texture;
    rhi::Texture m_render_texture;
    rhi::Texture m_velocity_texture;
    rhi::Texture m_ambient_occlusion_texture;
    rhi::Texture m_ambient_occlusion_blurred_horizontal_texture;
    rhi::Texture m_ambient_occlusion_blurred_vertical_texture;
    rhi::Texture m_ambient_occlusion_upsampled_texture;
    rhi::Texture m_shadow_texture;
    rhi::Texture m_shadow_pcf_jitter_texture;
    rhi::Texture m_fog_texture;
    rhi::Texture m_fog_blurred_horizontal_texture;
    rhi::Texture m_fog_blurred_vertical_texture;
    rhi::TextureDescriptor m_shadow_texture_descriptor_cascade[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    rhi::Texture m_depth_texture;
    rhi::Texture m_depth_view_quarter_res_texture;
    rhi::Texture m_depth_pyramid_texture;
    std::vector<rhi::TextureDescriptor> m_depth_pyramid_mip_texture_descriptors;
    rhi::Pipeline m_visibility_pipeline;
    rhi::Pipeline m_depth_view_quarter_res_pipeline;
    rhi::Pipeline m_ambient_occlusion_pipeline;
    rhi::Pipeline m_ambient_occlusion_blur_pipeline;
    rhi::Pipeline m_ambient_occlusion_upsample_pipeline;
    rhi::Pipeline m_fog_pipeline;
    rhi::Pipeline m_blur_fog_pipeline;
    rhi::Pipeline m_shadow_cull_pipeline;
    rhi::Pipeline m_shadow_pipeline;
    rhi::Pipeline m_visibility_render_pipeline;
    rhi::Pipeline m_fullscreen_pipeline;
    rhi::Pipeline m_cull_early_pipeline;
    rhi::Pipeline m_cull_late_pipeline;
    rhi::Pipeline m_taa_resolve_pipeline;
    rhi::Pipeline m_debug_grad_error_pipeline;
    rhi::Pipeline m_depth_reduce_pipeline;
    rhi::Pipeline m_debug_draw_rect_pipeline;
    rhi::Pipeline m_debug_draw_fill_cmd_pipeline;
    rhi::Buffer m_vertex_buffer;
    rhi::Buffer m_index_buffer;
    rhi::Buffer m_draw_cmd_buffer1;
    rhi::Buffer m_draw_cmd_early_buffer2;
    rhi::Buffer m_draw_cmd_late_buffer2;
    rhi::Buffer m_draw_cmd_shadow_buffer;
    rhi::Buffer m_draw_indices_early_buffer;
    rhi::Buffer m_draw_indices_late_buffer;
    rhi::Buffer m_draw_indices_shadow_buffer;
    rhi::Buffer m_material_buffer;
    rhi::Buffer m_draw_data_buffer;
    rhi::Buffer m_draw_count_buffer;
    rhi::Buffer m_mesh_primitive_visible_buffer;
    rhi::Buffer m_debug_draw_count_buffer;
    rhi::Buffer m_debug_draw_rect_buffer;
    rhi::Buffer m_debug_draw_cmd_buffer;
    ImguiRenderer m_imgui_renderer;
    rhi::Sampler m_texture_sampler;
    rhi::Sampler m_linear_sampler;
    rhi::Sampler m_nearest_sampler;
    rhi::Sampler m_min_sampler;
    rhi::Sampler m_shadow_sampler;
    rhi::Sampler m_shadow_pcf_jitter_sampler;
    std::vector<rhi::Texture> m_textures;
    Vec2U32 m_window_size;
    Frame m_frame[rhi::FRAMES_IN_FLIGHT];
    f32 m_shadow_cascade_radii[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    int m_frame_idx;
    int m_prev_frame_idx;
    u32 m_taa_jitter_idx;
    u32 m_taa_jitter_max_idx;
    UniformData m_uniform_data;
    ShadowPassData m_shadow_pass_data;
    PushConstantsCull m_cull_pass_data;
    PushConstantsSSAO m_ssao_pass_data;
    PushConstantsTAA m_taa_pass_data;
    bool m_new_frame_started;
    bool m_rendering_paused;
    bool m_swapchain_needs_recreating;
    bool m_swapchain_recreated;
    bool m_enable_ui;
    bool m_render_mode_changed;
    bool m_cull_camera_frozen;

    bool init();
    void cleanup();
    bool start_new_frame();
    bool render(f32 delta_time);
    void update_camera(Vec3 position, const Mat4& world_to_view);
    void pause_rendering(bool paused);
    void change_render_mode(RenderMode mode);
    void freeze_cull_camera(bool frozen);
    bool recompile_pipelines();
    void set_sun_direction(f32 yaw, f32 pitch);

private:
    bool upload_textures(const std::vector<std::string>& texture_paths);
    void update_shadow_cascades();

    void visibility_buffer_pass(rhi::CommandBuffer cb, bool cull_late);
    void cull_pass(rhi::CommandBuffer cb, bool late);
    void depth_reduce_pass(rhi::CommandBuffer cb);

    void depth_view_quarter_res_pass(rhi::CommandBuffer cb);
    void ambient_occlusion_pass(rhi::CommandBuffer cb);
    void ambient_occlusion_blur_pass(rhi::CommandBuffer cb, bool horizontal);
    void ambient_occlusion_upsample_pass(rhi::CommandBuffer cb);

    void shadow_cull_pass(rhi::CommandBuffer cb);
    void shadow_pass(rhi::CommandBuffer cb);

    void fog_pass(rhi::CommandBuffer cb);
    void blur_fog_pass(rhi::CommandBuffer cb, bool horizontal);

    void render_pass(rhi::CommandBuffer cb);
    void taa_resolve_pass(rhi::CommandBuffer cb);
    void debug_draw_pass(rhi::CommandBuffer cb);
    void fullscreen_pass(rhi::CommandBuffer cb, rhi::Texture swapchain_texture);

    void debug_draw_grad_error_pass(
        rhi::CommandBuffer cb,
        bool cull_late,
        rhi::Texture swapchain_texture
    );

    void record_and_submit_debug_grad_error(rhi::Texture swapchain_texture);
    void record_and_submit_visibility(rhi::Texture swapchain_texture);
    void create_swapchain(Vec2U32 size);
    void cleanup_swapchain();
    void create_color_resources();
    void cleanup_color_resources();
    void create_depth_resources();
    void cleanup_depth_resources();
    void cleanup_pipelines();
};
