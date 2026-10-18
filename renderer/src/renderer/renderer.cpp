#include "renderer.hpp"

#include "renderer/rhi/vulkan/type_convert.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cgltf.h>
#include <ktx.h>
#include <ktxvulkan.h>
#include <stdio.h>

#include "math/mat4.hpp"
#include "math/vec2.hpp"
#include "utils.hpp"

#define SDL_PRINT_ERROR(function_name) \
    fprintf(stderr, "%s:%d: " function_name " failed: %s\n", __FILE__, __LINE__, SDL_GetError())

static u32 calc_dispatch_size(u32 size, u32 local_size) {
    DEBUG_ASSERT(size > 0);
    DEBUG_ASSERT(local_size > 0);

    return (size + local_size - 1) / local_size;
}

static Vec3U32 calc_dispatch_size(Vec2U32 texture_size, Vec3U32 local_size) {
    DEBUG_ASSERT(texture_size.x > 0);
    DEBUG_ASSERT(texture_size.y > 0);
    DEBUG_ASSERT(local_size.x > 0);
    DEBUG_ASSERT(local_size.y > 0);
    DEBUG_ASSERT(local_size.z > 0);

    return {
        (texture_size.x + local_size.x - 1) / local_size.x,
        (texture_size.y + local_size.y - 1) / local_size.y,
        1
    };
}

static void cmd_dispatch_over_texture_size(
    rhi::CommandBuffer cb,
    rhi::Pipeline pipeline,
    rhi::Texture texture
) {
    rhi::cmd_dispatch(
        cb,
        calc_dispatch_size(
            rhi::get_texture_dimensions(texture).xy(),
            rhi::get_pipeline_local_size(pipeline)
        )
    );
}

[[maybe_unused]]
static void cmd_full_barrier(rhi::CommandBuffer cb) {
    rhi::cmd_barrier(
        cb,
        rhi::STAGE_ALL_COMMANDS_BIT,
        rhi::ACCESS_MEMORY_WRITE_BIT,
        rhi::STAGE_ALL_COMMANDS_BIT,
        rhi::ACCESS_MEMORY_READ_BIT | rhi::ACCESS_MEMORY_WRITE_BIT
    );
}

// GPU gems 2, Chapter 17, Efficient Soft-Edged Shadows Using Pixel Shader Branching, Yury Uralsky.
static eastl::vector<i8> create_shadow_jitter_offsets(
    size_t size,
    size_t samples_u,
    size_t samples_v
) {
    u32 random_u32 = 1337;

    eastl::vector<i8> result(size * size * samples_u * samples_v * 4 / 2);

    const size_t grid_size = samples_u * samples_v / 2;

    for (size_t i = 0; i < size; ++i) {
        for (size_t j = 0; j < size; ++j) {
            for (size_t k = 0; k < grid_size; ++k) {
                const size_t x = k % (samples_u / 2);
                const size_t y = (samples_v - 1) - k / (samples_u / 2);

                // Generate points on a regular rectangular grid size samples_u * samples_v.
                Vec4 grid_points{};
                grid_points[0] = (f32(x) * 2.0f + 0.5f) / f32(samples_u);
                grid_points[1] = (f32(y) + 0.5f) / f32(samples_v);
                grid_points[2] = (f32(x) * 2.0f + 1.0f + 0.5f) / f32(samples_u);
                grid_points[3] = grid_points[1];

                // Jitter position.
                grid_points[0] += math::lfsr_next_get_float(random_u32, 0.5f / f32(samples_u));
                grid_points[1] += math::lfsr_next_get_float(random_u32, 0.5f / f32(samples_v));
                grid_points[2] += math::lfsr_next_get_float(random_u32, 0.5f / f32(samples_u));
                grid_points[3] += math::lfsr_next_get_float(random_u32, 0.5f / f32(samples_v));

                // Warp jittered rectangular grid to disk.
                const Vec4 disk_points = {
                    sqrtf(grid_points[1]) * cosf(M_PIf * 2.0f * grid_points[0]),
                    sqrtf(grid_points[1]) * sinf(M_PIf * 2.0f * grid_points[0]),
                    sqrtf(grid_points[3]) * cosf(M_PIf * 2.0f * grid_points[2]),
                    sqrtf(grid_points[3]) * sinf(M_PIf * 2.0f * grid_points[2]),
                };

                result[(k * size * size + j * size + i) * 4 + 0] = i8(disk_points[0] * 127.0f);
                result[(k * size * size + j * size + i) * 4 + 1] = i8(disk_points[1] * 127.0f);
                result[(k * size * size + j * size + i) * 4 + 2] = i8(disk_points[2] * 127.0f);
                result[(k * size * size + j * size + i) * 4 + 3] = i8(disk_points[3] * 127.0f);
            }
        }
    }

    return result;
}

// Adapted from sky.hlsli, assuming view_direction_world == -sun_direction_world.
static Vec3 calc_sun_color(Vec3 sun_direction_world) {
    const f32 I0 = 11.0f;
    const Vec3 sigma_rayleigh = Vec3{0.33f, 0.78f, 1.89f};
    const f32 s_rayleigh = 0.17f;
    const f32 s_mie = 0.05f;
    const f32 s_sun = 0.07f;
    const f32 w_mie = 0.25f;
    const f32 w_sun = 0.0002f;

    const Vec3 view_direction_world = -sun_direction_world;

    const f32 dot_view_sun = math::dot(view_direction_world, sun_direction_world);
    // Tweaking a little to prevent divisions by zero.
    const f32 sun_y = sun_direction_world.y() + 1e-5f;
    const f32 view_y = view_direction_world.y() + 2e-5f;

    const f32 phase_rayleigh = 3.0f / (16.0f * M_PIf) * (1.0f + dot_view_sun * dot_view_sun);

    f32 phase_mie = w_mie / (1.0f + w_mie + dot_view_sun);
    phase_mie = 0.3f * phase_mie * phase_mie;

    f32 phase_sun = w_sun / (1.0f + w_sun + dot_view_sun);
    phase_sun = 4.0f * phase_sun * phase_sun;

    const Vec3 sigma_sum = s_rayleigh * sigma_rayleigh + Vec3{s_mie};
    const Vec3 phase_sum =
        s_rayleigh * sigma_rayleigh * phase_rayleigh + Vec3{s_mie * phase_mie + s_sun * phase_sun};

    const f32 surface_darkening = view_direction_world.y() < 0.0f ? 0.3f : 1.0f;

    return I0 * surface_darkening * sun_y / (sun_y + view_y) * (phase_sum / sigma_sum)
        * (math::exp(sigma_sum / sun_y) - math::exp(-sigma_sum / abs(view_y)));
}

bool Renderer::init() {
    // SDL.
    {
#ifdef __linux__
        if (!SDL_SetHint("SDL_VIDEO_DRIVER", "x11")) {
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

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            SDL_PRINT_ERROR("SDL_InitSubSystem");
            return false;
        }

        m_window = SDL_CreateWindow(
            "renderer",
            800,
            600,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MOUSE_GRABBED
                | SDL_WINDOW_MOUSE_RELATIVE_MODE | SDL_WINDOW_FULLSCREEN
        );
        if (!m_window) {
            SDL_PRINT_ERROR("SDL_CreateWindow");
            return false;
        }

        (void)SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        (void)SDL_SetWindowRelativeMouseMode(m_window, true);
    }

    rhi::create(m_window);

    m_graphics_queue = rhi::create_queue({
        .type = rhi::QUEUE_TYPE_GRAPHICS,
        .debug_name = "graphics_queue",
    });
    m_compute_queue = rhi::create_queue({
        .type = rhi::QUEUE_TYPE_COMPUTE,
        .debug_name = "compute_queue",
    });
    m_transfer_queue = rhi::create_queue({
        .type = rhi::QUEUE_TYPE_TRANSFER,
        .debug_name = "transfer_queue",
    });

    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i) {
        m_frame[i].graphics_command_pool = rhi::create_command_pool({
            .queue = m_graphics_queue,
            .debug_name = "graphics_command_pool",
        });
        m_frame[i].compute_command_pool = rhi::create_command_pool({
            .queue = m_compute_queue,
            .debug_name = "compute_command_pool",
        });
        m_frame[i].transfer_command_pool = rhi::create_command_pool({
            .queue = m_transfer_queue,
            .debug_name = "transfer_command_pool",
        });
    }

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(m_window, &width, &height)) {
        SDL_PRINT_ERROR("SDL_GetWindowSizeInPixels");
        return false;
    }

    create_swapchain({u32(width), u32(height)});

    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i) {
        m_frame[i].uniform_buffer_manager.init(
            16'000,
            int(rhi::get_device_properties().min_uniform_buffer_offset_alignment)
        );
    }

    // Buffers.
    {
        m_draw_count_buffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32),
            .debug_name = "draw_count_buffer",
        });

        m_mesh_primitive_visible_buffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debug_name = "mesh_primitive_visible_buffer",
        });

        m_debug_draw_count_buffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * 1, // TODO: maybe enum max count for offsets?
            .debug_name = "debug_draw_count_buffer",
        });

        m_debug_draw_rect_buffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(DebugDrawRectData) * RENDERER_DEBUG_DRAW_RECT_MAX_COUNT,
            .debug_name = "debug_draw_rect_buffer",
        });

        m_debug_draw_cmd_buffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(rhi::DrawIndirectCommand),
            .debug_name = "debug_draw_cmd_buffer",
        });
    }

    m_shadow_texture = rhi::create_texture({
        .format = rhi::FORMAT_D16_UNORM,
        .type = rhi::TEXTURE_TYPE_2D_ARRAY,
        .dimensions = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS, 1},
        .layer_count = RENDERER_SHADOW_MAP_CASCADE_COUNT,
        .usage = rhi::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "shadow_texture",
    });

    if (!recompile_pipelines()) {
        return false;
    }

    // Synchronization primitives.
    {
        m_frame_semaphore = rhi::create_semaphore(0);

        for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i) {
            m_frame[i].start_semaphore.semaphore = rhi::create_semaphore(0);
            m_frame[i].shadow_semaphore.semaphore = rhi::create_semaphore(0);
            m_frame[i].ssao_semaphore.semaphore = rhi::create_semaphore(0);
        }
    }

    // Shadow map resources.
    {
        // One texture descriptor per cascade.
        for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i) {
            m_shadow_texture_descriptor_cascade[i] = rhi::create_texture_descriptor({
                .texture = m_shadow_texture,
                .type = rhi::TEXTURE_TYPE_2D,
                .base_layer = u32(i),
                .layer_count = 1,
            });
        }

        m_shadow_sampler = rhi::create_sampler({
            .compare_enable = true,
            .compare_op = rhi::COMPARE_OP_GREATER,
        });

        // PCF jitter offsets.
        {
            m_shadow_pcf_jitter_texture = rhi::create_texture({
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
                .debug_name = "shadow_pcf_jitter_texture",
            });

            const eastl::vector<i8> jitter_offsets = create_shadow_jitter_offsets(
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE,
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U,
                RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V
            );

            const u64 upload_size = VEC_SIZE_BYTES(jitter_offsets);
            rhi::Buffer staging_buffer = rhi::create_buffer({
                .size = upload_size,
                .debug_name = "staging_buffer",
            });
            DEFER(rhi::destroy_buffer(staging_buffer));

            memcpy(rhi::get_buffer_host_ptr(staging_buffer), jitter_offsets.data(), upload_size);

            rhi::CommandBuffer cb = rhi::create_command_buffer({
                .command_pool = m_frame[0].transfer_command_pool,
                .debug_name = "upload_shadow_pcf_jitter_cb",
            });
            DEFER(rhi::destroy_command_buffer(cb));

            rhi::begin_command_buffer(cb);

            rhi::cmd_texture_barrier(
                cb,
                {{
                    .texture = m_shadow_pcf_jitter_texture,
                    .old_layout = rhi::TEXTURE_LAYOUT_UNDEFINED,
                    .new_layout = rhi::TEXTURE_LAYOUT_GENERAL,
                    .src_stage_mask = rhi::STAGE_HOST_BIT,
                    .dst_stage_mask = rhi::STAGE_ALL_TRANSFER_BIT,
                    .dst_access_mask = rhi::ACCESS_TRANSFER_WRITE_BIT,
                }}
            );

            rhi::cmd_copy_buffer_to_texture(
                cb,
                staging_buffer,
                m_shadow_pcf_jitter_texture,
                {{
                    .texture_dimensions = {
                        u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE),
                        u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE),
                        u32(RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U
                            * RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V / 2),
                    },
                }}
            );

            rhi::end_command_buffer(cb);

            rhi::queue_submit(m_transfer_queue, {{.cb = cb}});

            rhi::device_wait_idle();

            m_shadow_pcf_jitter_sampler = rhi::create_sampler({
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
        m_texture_sampler = rhi::create_sampler({
            .address_mode_u = rhi::SAMPLER_ADDRESS_MODE_REPEAT,
            .address_mode_v = rhi::SAMPLER_ADDRESS_MODE_REPEAT,
            .address_mode_w = rhi::SAMPLER_ADDRESS_MODE_REPEAT,
            .anisotropy_enable = true,
            .max_anisotropy = 4.0f,
            .max_lod = 16.0f,
        });

        m_linear_sampler = rhi::create_sampler({});

        m_nearest_sampler = rhi::create_sampler({
            .mag_filter = rhi::FILTER_NEAREST,
            .min_filter = rhi::FILTER_NEAREST,
        });

        m_min_sampler = rhi::create_sampler({
            .reduction_mode = rhi::SAMPLER_REDUCTION_MODE_MIN,
            .max_lod = 16.0f,
        });
    }

    // Scene.
    {
        const eastl::string gltf_path = "../assets/main_sponza/NewSponza_Main_glTF_003.gltf";

        eastl::vector<Vertex> vertices;
        eastl::vector<u32> indices;
        eastl::vector<MeshPrimitive> mesh_primitives;
        eastl::vector<rhi::DrawIndexedIndirectCommand> draw_cmds;
        eastl::vector<DrawData> draw_data;
        eastl::vector<Material> materials;
        eastl::vector<eastl::string> texture_paths;

        if (!load_scene(
                vertices,
                indices,
                mesh_primitives,
                draw_cmds,
                draw_data,
                materials,
                texture_paths,
                m_uniform_data.sun_direction_world,
                gltf_path
            ))
        {
            fprintf(stderr, "scene loading failed, path: %s\n", gltf_path.c_str());
            return false;
        }

        m_uniform_data.draw_count = u32(draw_cmds.size());

        if (!upload_textures(texture_paths)) {
            fprintf(stderr, "vulkan: texture uploading failed\n");
            return false;
        }

        m_vertex_buffer = rhi::create_buffer({
            .size = VEC_SIZE_BYTES(vertices),
            .debug_name = "vertex_buffer",
        });
        memcpy(
            rhi::get_buffer_host_ptr(m_vertex_buffer),
            vertices.data(),
            VEC_SIZE_BYTES(vertices)
        );
        rhi::unmap_buffer(m_vertex_buffer);

        m_index_buffer = rhi::create_buffer({
            .size = VEC_SIZE_BYTES(indices),
            .debug_name = "index_buffer",
        });
        memcpy(rhi::get_buffer_host_ptr(m_index_buffer), indices.data(), VEC_SIZE_BYTES(indices));
        rhi::unmap_buffer(m_index_buffer);

        m_draw_cmd_buffer1 = rhi::create_buffer({
            .size = sizeof(rhi::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debug_name = "draw_cmd_buffer1",
        });

        m_draw_cmd_early_buffer2 = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(rhi::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debug_name = "draw_cmd_early_buffer2",
        });

        m_draw_cmd_late_buffer2 = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(rhi::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debug_name = "draw_cmd_late_buffer2",
        });

        m_draw_cmd_shadow_buffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(rhi::DrawIndexedIndirectCommand) * MAX_DRAW_CALLS,
            .debug_name = "draw_cmd_shadow_buffer",
        });

        m_draw_indices_early_buffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debug_name = "draw_indices_early_buffer",
        });

        m_draw_indices_late_buffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debug_name = "draw_indices_late_buffer",
        });

        m_draw_indices_shadow_buffer = rhi::create_buffer({
            .type = rhi::MEMORY_TYPE_DEVICE,
            .size = sizeof(u32) * MAX_DRAW_CALLS,
            .debug_name = "draw_indices_shadow_buffer",
        });

        m_material_buffer = rhi::create_buffer({
            .size = sizeof(Material) * MAX_DRAW_CALLS,
            .debug_name = "material_buffer",
        });

        m_draw_data_buffer = rhi::create_buffer({
            .size = sizeof(DrawData) * MAX_DRAW_CALLS,
            .debug_name = "draw_data_buffer",
        });

        memcpy(
            rhi::get_buffer_host_ptr(m_draw_cmd_buffer1),
            draw_cmds.data(),
            VEC_SIZE_BYTES(draw_cmds)
        );

        memcpy(
            rhi::get_buffer_host_ptr(m_material_buffer),
            materials.data(),
            VEC_SIZE_BYTES(materials)
        );

        memcpy(
            rhi::get_buffer_host_ptr(m_draw_data_buffer),
            draw_data.data(),
            VEC_SIZE_BYTES(draw_data)
        );
    }

    // Command buffers
    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i) {
        m_frame[i].start_command_buffer = rhi::create_command_buffer({
            .command_pool = m_frame[i].graphics_command_pool,
            .debug_name = "start_cb",
        });
        m_frame[i].shadow_command_buffer = rhi::create_command_buffer({
            .command_pool = m_frame[i].graphics_command_pool,
            .debug_name = "shadow_cb",
        });
        m_frame[i].ssao_command_buffer = rhi::create_command_buffer({
            .command_pool = m_frame[i].compute_command_pool,
            .debug_name = "ssao_cb",
        });
        m_frame[i].end_command_buffer = rhi::create_command_buffer({
            .command_pool = m_frame[i].graphics_command_pool,
            .debug_name = "end_cb",
        });
    }

    if (!m_imgui_renderer.init(
            m_window,
            rhi::get_texture_format(rhi::get_swapchain_texture(0)),
            m_transfer_queue,
            m_frame[0].transfer_command_pool
        ))
    {
        fprintf(stderr, "failed to initialize ImGui renderer\n");
        return false;
    }

    // Initializing resources.
    {
        rhi::CommandBuffer cb = rhi::create_command_buffer({
            .command_pool = m_frame[0].transfer_command_pool,
            .debug_name = "init_cb",
        });
        DEFER(rhi::destroy_command_buffer(cb));

        rhi::begin_command_buffer(cb);

        rhi::cmd_fill_buffer(
            cb,
            m_mesh_primitive_visible_buffer,
            0,
            sizeof(u32) * MAX_DRAW_CALLS,
            0
        );

        rhi::end_command_buffer(cb);

        rhi::queue_submit(m_transfer_queue, {{.cb = cb}});
        rhi::queue_wait_idle(m_transfer_queue);
    }

    m_swapchain_needs_recreating = true;
    m_taa_jitter_max_idx = 8;
    m_taa_pass_data.blend_weight = 0.1f;
    m_uniform_data.ambient_intensity = 0.04f;
    m_uniform_data.sun_intensity = 1.0f;
    m_uniform_data.grad_error_max = 0.01f;
    m_uniform_data.enable_ssao = true;
    m_uniform_data.enable_fog = true;

    m_shadow_pass_data.enable_pcf = 1;
    m_shadow_pass_data.pcf_kernel_scale = 3.0f;
    m_shadow_pass_data.pcf_kernel_cascade_scales[0] = 1.0f;
    m_shadow_pass_data.pcf_kernel_cascade_scales[1] = 0.40f;
    m_shadow_pass_data.pcf_kernel_cascade_scales[2] = 0.0f;
    m_shadow_pass_data.pcf_kernel_cascade_scales[3] = 0.0f;
    m_shadow_pass_data.normal_offset = 1.0f;
    m_shadow_pass_data.constant_offset = 0.00003f;

    // TODO: try SDSM? In theory, due to 2-pass occlusion culling we have a depth pyramid
    // of last visible objects (just add a max sampler too), not sure if it's ok to use it,
    // but making another depth pyramid sounds stupid (unless it can be useful for other stuff?).
    // TODO: or, if I use mesh shaders, virtual shadow mapping can be very efficient,
    // and it looks great because of high resolution.
    m_shadow_cascade_radii[0] = 3.0f;
    m_shadow_cascade_radii[1] = 6.0f;
    m_shadow_cascade_radii[2] = 10.0f;
    m_shadow_cascade_radii[3] = 20.0f;

    return true;
}

void Renderer::cleanup() {
    rhi::device_wait_idle();

    rhi::destroy_queue(m_graphics_queue);
    rhi::destroy_queue(m_compute_queue);
    rhi::destroy_queue(m_transfer_queue);

    m_imgui_renderer.cleanup();

    cleanup_pipelines();
    cleanup_color_resources();
    cleanup_depth_resources();

    for (rhi::Texture& tex: m_textures) {
        rhi::destroy_texture(tex);
    }

    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i) {
        rhi::destroy_texture_descriptor(m_shadow_texture_descriptor_cascade[i]);
    }

    rhi::destroy_texture(m_shadow_texture);
    rhi::destroy_texture(m_shadow_pcf_jitter_texture);
    rhi::destroy_buffer(m_debug_draw_cmd_buffer);
    rhi::destroy_buffer(m_debug_draw_rect_buffer);
    rhi::destroy_buffer(m_debug_draw_count_buffer);
    rhi::destroy_buffer(m_mesh_primitive_visible_buffer);
    rhi::destroy_buffer(m_draw_count_buffer);
    rhi::destroy_buffer(m_material_buffer);
    rhi::destroy_buffer(m_draw_data_buffer);
    rhi::destroy_buffer(m_vertex_buffer);
    rhi::destroy_buffer(m_index_buffer);
    rhi::destroy_buffer(m_draw_indices_shadow_buffer);
    rhi::destroy_buffer(m_draw_indices_early_buffer);
    rhi::destroy_buffer(m_draw_indices_late_buffer);
    rhi::destroy_buffer(m_draw_cmd_shadow_buffer);
    rhi::destroy_buffer(m_draw_cmd_early_buffer2);
    rhi::destroy_buffer(m_draw_cmd_late_buffer2);
    rhi::destroy_buffer(m_draw_cmd_buffer1);
    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i) {
        m_frame[i].uniform_buffer_manager.cleanup();
    }
    rhi::destroy_semaphore(m_frame_semaphore);
    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i) {
        rhi::destroy_command_pool(m_frame[i].graphics_command_pool);
        rhi::destroy_command_pool(m_frame[i].compute_command_pool);
        rhi::destroy_command_pool(m_frame[i].transfer_command_pool);

        rhi::destroy_semaphore(m_frame[i].start_semaphore.semaphore);
        rhi::destroy_semaphore(m_frame[i].shadow_semaphore.semaphore);
        rhi::destroy_semaphore(m_frame[i].ssao_semaphore.semaphore);
    }
    rhi::destroy_sampler(m_shadow_pcf_jitter_sampler);
    rhi::destroy_sampler(m_shadow_sampler);
    rhi::destroy_sampler(m_min_sampler);
    rhi::destroy_sampler(m_nearest_sampler);
    rhi::destroy_sampler(m_linear_sampler);
    rhi::destroy_sampler(m_texture_sampler);
    cleanup_swapchain();
    rhi::destroy();
}

bool Renderer::start_new_frame() {
    DEBUG_ASSERT(!m_new_frame_started);

    if (m_rendering_paused) {
        return true;
    }

    if (m_uniform_data.frame_count >= rhi::FRAMES_IN_FLIGHT) {
        rhi::wait_semaphore(
            m_frame_semaphore,
            m_uniform_data.frame_count - rhi::FRAMES_IN_FLIGHT + 1
        );
    }

    rhi::begin_new_frame(m_frame_idx);

    rhi::reset_command_pool(m_frame[m_frame_idx].graphics_command_pool);
    rhi::reset_command_pool(m_frame[m_frame_idx].compute_command_pool);

    m_imgui_renderer.start_new_frame();

    m_new_frame_started = true;

    return true;
}

bool Renderer::render(f32 delta_time) {
    DEBUG_ASSERT(delta_time > 0.0f);

    if (m_rendering_paused) {
        SDL_Delay(100);
        return true;
    }

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(m_window, &width, &height)) {
        SDL_PRINT_ERROR("SDL_GetWindowSizeInPixels");
        return false;
    }
    if ((m_window_size.x != u32(width)) || (m_window_size.y != u32(height))) {
        m_window_size = {u32(width), u32(height)};
        m_swapchain_needs_recreating = true;
    }

    if (m_swapchain_needs_recreating) {
        m_swapchain_needs_recreating = false;
        create_swapchain(m_window_size);
    }

    Frame& frame = m_frame[m_frame_idx];

    // I can't be arsed to handle the edge cases and it's only used for debugging.
    // TODO: Memory visibility/availability should be unnecessary I think, remove queue_submit.
    if (m_render_mode_changed) {
        rhi::device_wait_idle();

        rhi::CommandBuffer cb = rhi::create_command_buffer({
            .command_pool = m_frame[0].graphics_command_pool,
            .debug_name = "rendering_mode_changed_cb",
        });
        DEFER(rhi::destroy_command_buffer(cb));

        rhi::begin_command_buffer(cb);

        rhi::end_command_buffer(cb);

        rhi::queue_submit(m_graphics_queue, {{.cb = cb}});

        rhi::device_wait_idle();
    }

    rhi::Texture swapchain_texture_handle{};
    rhi::SwapchainResult swapchain_result =
        rhi::acquire_next_swapchain_texture(swapchain_texture_handle);

    if (swapchain_result == rhi::SWAPCHAIN_OUT_OF_DATE) {
        m_swapchain_needs_recreating = true;
        return true;
    } else if (
        (swapchain_result != rhi::SWAPCHAIN_SUCCESS)
        && (swapchain_result != rhi::SWAPCHAIN_SUBOPTIMAL)
    )
    {
        fprintf(stderr, "rhi::acquire_next_swapchain_texture failed\n");
        return false;
    }

    m_uniform_data.delta_time = delta_time;

    if ((m_taa_pass_data.enable == 1) && (m_uniform_data.render_mode != RENDER_MODE_GRAD_ERROR)) {
        const f32 halton_x = 2.0f * math::halton_sequence(m_taa_jitter_idx + 1, 2) - 1.0f;
        const f32 halton_y = 2.0f * math::halton_sequence(m_taa_jitter_idx + 1, 3) - 1.0f;
        m_uniform_data.prev_taa_jitter = m_uniform_data.taa_jitter;
        m_uniform_data.taa_jitter = {
            halton_x / f32(m_uniform_data.render_width),
            halton_y / f32(m_uniform_data.render_height),
        };

        m_taa_jitter_idx = (m_taa_jitter_idx + 1) % m_taa_jitter_max_idx;
        // To derive, construct a translation jitter matrix and multiply with view_to_clip.
        m_uniform_data.view_to_clip(0, 2) = -m_uniform_data.taa_jitter.x();
        m_uniform_data.view_to_clip(1, 2) = -m_uniform_data.taa_jitter.y();
    } else {
        m_uniform_data.view_to_clip(0, 2) = 0.0f;
        m_uniform_data.view_to_clip(1, 2) = 0.0f;
    }

    m_uniform_data.prev_world_to_clip = m_uniform_data.world_to_clip;
    m_uniform_data.world_to_clip = m_uniform_data.view_to_clip * m_uniform_data.world_to_view;
    m_uniform_data.clip_to_world = math::inverse(m_uniform_data.world_to_clip);

    // https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/
    // Also, niagara:
    // https://github.com/zeux/niagara
    if (!m_cull_camera_frozen) {
        const Mat4 view_to_clip_transposed = math::transpose(m_uniform_data.view_to_clip);

        // -w <= x; x + w >= 0
        const Vec4 frustum_plane_x =
            math::normalize_plane(view_to_clip_transposed.col[0] + view_to_clip_transposed.col[3]);
        // -w <= y; y + w >= 0
        const Vec4 frustum_plane_y =
            math::normalize_plane(view_to_clip_transposed.col[1] + view_to_clip_transposed.col[3]);

        m_cull_pass_data.frustum_plane_xx = frustum_plane_x.x();
        m_cull_pass_data.frustum_plane_xz = frustum_plane_x.z();
        m_cull_pass_data.frustum_plane_yy = frustum_plane_y.y();
        m_cull_pass_data.frustum_plane_yz = frustum_plane_y.z();

        m_uniform_data.cull_world_to_view = m_uniform_data.world_to_view;
    }

    update_shadow_cascades();

    frame.uniform_buffer_manager.on_new_frame();
    m_uniform_buffer_descriptor =
        frame.uniform_buffer_manager.push(&m_uniform_data, sizeof(m_uniform_data));
    m_shadow_pass_uniform_buffer_descriptor =
        frame.uniform_buffer_manager.push(&m_shadow_pass_data, sizeof(m_shadow_pass_data));

    switch (m_uniform_data.render_mode) {
        case RENDER_MODE_GRAD_ERROR:
            record_and_submit_debug_grad_error(swapchain_texture_handle);
            break;
        default:
            record_and_submit_visibility(swapchain_texture_handle);
            break;
    }

    swapchain_result = rhi::queue_present(m_graphics_queue);
    if ((swapchain_result == rhi::SWAPCHAIN_OUT_OF_DATE)
        || (swapchain_result == rhi::SWAPCHAIN_SUBOPTIMAL))
    {
        m_swapchain_needs_recreating = true;
    } else if (swapchain_result != rhi::SWAPCHAIN_SUCCESS) {
        fprintf(stderr, "RHI::QueuePresent failed\n");
        return false;
    }

    m_new_frame_started = false;

    m_prev_frame_idx = m_frame_idx;
    m_frame_idx = (m_frame_idx + 1) % rhi::FRAMES_IN_FLIGHT;
    ++m_uniform_data.frame_count;
    m_swapchain_recreated = false;
    m_render_mode_changed = false;

    return true;
}

void Renderer::update_camera(Vec3 position, const Mat4& world_to_view) {
    m_uniform_data.camera_position = position;
    m_uniform_data.world_to_view = world_to_view;
}

void Renderer::pause_rendering(bool paused) {
    m_rendering_paused = paused;
}

void Renderer::change_render_mode(RenderMode mode) {
    if (m_uniform_data.render_mode != static_cast<u32>(mode)) {
        m_render_mode_changed = true;
        m_uniform_data.render_mode = static_cast<u32>(mode);
    }
}

void Renderer::freeze_cull_camera(bool frozen) {
    m_cull_camera_frozen = frozen;
}

void Renderer::cleanup_pipelines() {
    rhi::destroy_pipeline(m_shadow_cull_pipeline);
    rhi::destroy_pipeline(m_shadow_pipeline);
    rhi::destroy_pipeline(m_blur_fog_pipeline);
    rhi::destroy_pipeline(m_fog_pipeline);
    rhi::destroy_pipeline(m_ambient_occlusion_upsample_pipeline);
    rhi::destroy_pipeline(m_ambient_occlusion_blur_pipeline);
    rhi::destroy_pipeline(m_ambient_occlusion_pipeline);
    rhi::destroy_pipeline(m_depth_view_quarter_res_pipeline);
    rhi::destroy_pipeline(m_debug_draw_fill_cmd_pipeline);
    rhi::destroy_pipeline(m_debug_draw_rect_pipeline);
    rhi::destroy_pipeline(m_depth_reduce_pipeline);
    rhi::destroy_pipeline(m_debug_grad_error_pipeline);
    rhi::destroy_pipeline(m_taa_resolve_pipeline);
    rhi::destroy_pipeline(m_cull_late_pipeline);
    rhi::destroy_pipeline(m_cull_early_pipeline);
    rhi::destroy_pipeline(m_fullscreen_pipeline);
    rhi::destroy_pipeline(m_visibility_render_pipeline);
    rhi::destroy_pipeline(m_visibility_pipeline);
}

bool Renderer::recompile_pipelines() {
    rhi::device_wait_idle();

    cleanup_pipelines();

    // Graphics pipelines
    {
        utils::FileData vert_data = utils::file_read("visibility_buffer.vert.hlsl.spv");
        DEFER(free(vert_data.data));
        utils::FileData frag_data = utils::file_read("visibility_buffer.frag.hlsl.spv");
        DEFER(free(frag_data.data));

        m_visibility_pipeline = rhi::create_graphics_pipeline({
            .bytecodes =
                {
                    {static_cast<u8*>(vert_data.data), int(vert_data.size)},
                    {static_cast<u8*>(frag_data.data), int(frag_data.size)},
                },
            .cull = rhi::CULL_CW,
            .depth_format = rhi::get_texture_format(m_depth_texture),
            .depth_mask = rhi::DEPTH_READ_BIT | rhi::DEPTH_WRITE_BIT,
            .color_targets = {{
                .format = rhi::get_texture_format(m_visibility_texture),
                .color_component_mask = rhi::COLOR_COMPONENT_R_BIT | rhi::COLOR_COMPONENT_G_BIT,
            }},
            .debug_name = "visibility_buffer_pass",
        });
        if (!m_visibility_pipeline) {
            return false;
        }
    }

    {
        utils::FileData vert_data = utils::file_read("debug_draw_rect.vert.hlsl.spv");
        DEFER(free(vert_data.data));
        utils::FileData frag_data = utils::file_read("debug_draw_rect.frag.hlsl.spv");
        DEFER(free(frag_data.data));

        m_debug_draw_rect_pipeline = rhi::create_graphics_pipeline({
            .bytecodes =
                {
                    {static_cast<u8*>(vert_data.data), int(vert_data.size)},
                    {static_cast<u8*>(frag_data.data), int(frag_data.size)},
                },
            .color_targets =
                {
                    {.format = rhi::get_texture_format(m_render_texture)},
                },
            .debug_name = "debug_draw_rect_pass",
        });
        if (!m_debug_draw_rect_pipeline) {
            return false;
        }
    }

    {
        utils::FileData vert_data = utils::file_read("fullscreen.vert.hlsl.spv");
        DEFER(free(vert_data.data));
        utils::FileData frag_data = utils::file_read("fullscreen.frag.hlsl.spv");
        DEFER(free(frag_data.data));

        m_fullscreen_pipeline = rhi::create_graphics_pipeline({
            .bytecodes =
                {
                    {static_cast<u8*>(vert_data.data), int(vert_data.size)},
                    {static_cast<u8*>(frag_data.data), int(frag_data.size)},
                },
            .color_targets = {{.format = rhi::get_texture_format(rhi::get_swapchain_texture(0))}},
            .debug_name = "fullscreen_pass",
        });
        if (!m_fullscreen_pipeline) {
            return false;
        }
    }

    {
        utils::FileData vert_data = utils::file_read("debug_grad_error.vert.hlsl.spv");
        DEFER(free(vert_data.data));
        utils::FileData frag_data = utils::file_read("debug_grad_error.frag.hlsl.spv");
        DEFER(free(frag_data.data));

        m_debug_grad_error_pipeline = rhi::create_graphics_pipeline({
            .bytecodes =
                {
                    {static_cast<u8*>(vert_data.data), int(vert_data.size)},
                    {static_cast<u8*>(frag_data.data), int(frag_data.size)},
                },
            .cull = rhi::CULL_CW,
            .depth_format = rhi::get_texture_format(m_depth_texture),
            .depth_mask = rhi::DEPTH_READ_BIT | rhi::DEPTH_WRITE_BIT,
            .color_targets = {{.format = rhi::get_texture_format(rhi::get_swapchain_texture(0))}},
            .debug_name = "debug_grad_error_pass",
        });
        if (!m_debug_grad_error_pipeline) {
            return false;
        }
    }

    {
        utils::FileData vert_data = utils::file_read("shadow.vert.hlsl.spv");
        DEFER(free(vert_data.data));
        utils::FileData frag_data = utils::file_read("shadow.frag.hlsl.spv");
        DEFER(free(frag_data.data));

        m_shadow_pipeline = rhi::create_graphics_pipeline({
            .bytecodes =
                {
                    {static_cast<u8*>(vert_data.data), int(vert_data.size)},
                    {static_cast<u8*>(frag_data.data), int(frag_data.size)},
                },
            .cull = rhi::CULL_CW,
            .depth_format = rhi::get_texture_format(m_shadow_texture),
            .depth_mask = rhi::DEPTH_READ_BIT | rhi::DEPTH_WRITE_BIT,
            .depth_clamp_enable = true,
            .debug_name = "shadow_pass",
        });
        if (!m_shadow_pipeline) {
            return false;
        }
    }

    // Compute pipelines.
    {
        utils::FileData comp_data = utils::file_read("cull_early.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_cull_early_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "cull_early_pass",
        });
        if (!m_cull_early_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("cull_late.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_cull_late_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "cull_late_pass",
        });
        if (!m_cull_late_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("shadow_cull.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_shadow_cull_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "shadow_cull_pass",
        });
        if (!m_cull_late_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("visibility_render.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_visibility_render_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .uses_bindless_textures = true,
            .debug_name = "visibility_render_pass",
        });
        if (!m_visibility_render_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("taa_resolve.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_taa_resolve_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "taa_resolve_pass",
        });
        if (!m_taa_resolve_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("depth_reduce.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_depth_reduce_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "depth_reduce_pass",
        });
        if (!m_depth_reduce_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("depth_view_quarter_res.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_depth_view_quarter_res_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "depth_view_quarter_res_pass",
        });
        if (!m_depth_view_quarter_res_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("ssao.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_ambient_occlusion_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "ambient_occlusion_pass",
        });
        if (!m_ambient_occlusion_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("blur_ssao.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_ambient_occlusion_blur_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "ambient_occlusion_blur_pass",
        });
        if (!m_ambient_occlusion_blur_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("upsample_ssao.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_ambient_occlusion_upsample_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "ambient_occlusion_upsample_pass",
        });
        if (!m_ambient_occlusion_upsample_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("fog.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_fog_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "fog_pass",
        });
        if (!m_fog_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("blur_fog.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_blur_fog_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "blur_fog_pass",
        });
        if (!m_blur_fog_pipeline) {
            return false;
        }
    }

    {
        utils::FileData comp_data = utils::file_read("debug_draw_fill_cmd.comp.hlsl.spv");
        DEFER(free(comp_data.data));

        m_debug_draw_fill_cmd_pipeline = rhi::create_compute_pipeline({
            .bytecode = {static_cast<u8*>(comp_data.data), int(comp_data.size)},
            .debug_name = "debug_draw_fill_cmd_pass",
        });
        if (!m_debug_draw_fill_cmd_pipeline) {
            return false;
        }
    }

    return true;
}

void Renderer::set_sun_direction(f32 yaw, f32 pitch) {
    DEBUG_ASSERT(yaw >= 0.0f);
    DEBUG_ASSERT(pitch >= 0.0f);

    m_uniform_data.sun_direction_world.x() = sinf(yaw) * cosf(pitch);
    m_uniform_data.sun_direction_world.y() = -sinf(pitch);
    m_uniform_data.sun_direction_world.z() = cosf(yaw) * cosf(pitch);

    m_uniform_data.sun_color = calc_sun_color(m_uniform_data.sun_direction_world);
}

bool Renderer::upload_textures(const eastl::vector<eastl::string>& texture_paths) {
    DEBUG_ASSERT(!texture_paths.empty());

    m_textures.resize(texture_paths.size());

    for (size_t i = 0; i < texture_paths.size(); ++i) {
        ktxTexture2* ktx_tex{};
        ktx_error_code_e ktx_result = ktxTexture2_CreateFromNamedFile(
            texture_paths[i].c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &ktx_tex
        );
        if (ktx_result != KTX_SUCCESS) {
            fprintf(
                stderr,
                "ktx2 %s loading error: %s\n",
                texture_paths[i].c_str(),
                ktxErrorString(ktx_result)
            );
            return false;
        }
        DEFER(ktxTexture2_Destroy(ktx_tex));

        const rhi::Format format = format_to_rhi(ktxTexture2_GetVkFormat(ktx_tex));
        const Vec3U32 extent{ktx_tex->baseWidth, ktx_tex->baseHeight, ktx_tex->baseDepth};
        const u32 mip_levels = ktx_tex->numLevels;
        const ktx_size_t size = ktxTexture_GetDataSize(ktxTexture(ktx_tex));
        const ktx_uint8_t* ktx_data = ktxTexture_GetData(ktxTexture(ktx_tex));

        m_textures[i] = rhi::create_texture({
            .format = format,
            .dimensions = {ktx_tex->baseWidth, ktx_tex->baseHeight, 1},
            .mip_count = mip_levels,
            .usage = rhi::TEXTURE_USAGE_SAMPLED_BIT | rhi::TEXTURE_USAGE_TRANSFER_DST_BIT,
            .debug_name = texture_paths[i].c_str(),
        });

        rhi::Buffer staging_buffer = rhi::create_buffer({
            .size = size,
            .debug_name = "StagingBuffer",
        });
        DEFER(rhi::destroy_buffer(staging_buffer));

        eastl::vector<rhi::BufferTextureCopy> copy_regions(mip_levels);

        for (u32 mip_level = 0; mip_level < mip_levels; ++mip_level) {
            ktx_size_t offset = 0;
            ktx_result = ktxTexture2_GetImageOffset(ktx_tex, mip_level, 0, 0, &offset);
            if (ktx_result != KTX_SUCCESS) {
                fprintf(
                    stderr,
                    "ktxTexture_GetImageOffset failed: %s\n",
                    ktxErrorString(ktx_result)
                );
                return false;
            }

            copy_regions[mip_level] = {
                .buffer_offset = offset,
                .texture_subresource = {.mip_level = mip_level},
                .texture_dimensions = {extent.x >> mip_level, extent.y >> mip_level, 1},
            };
        }
        memcpy(rhi::get_buffer_host_ptr(staging_buffer), ktx_data, size);
        rhi::unmap_buffer(staging_buffer);

        rhi::CommandBuffer cb = rhi::create_command_buffer({
            .command_pool = m_frame[0].transfer_command_pool,
            .debug_name = "upload_textures_cb",
        });
        DEFER(rhi::destroy_command_buffer(cb));

        rhi::begin_command_buffer(cb);

        rhi::cmd_texture_barrier(
            cb,
            {{
                .texture = m_textures[i],
                .old_layout = rhi::TEXTURE_LAYOUT_UNDEFINED,
                .new_layout = rhi::TEXTURE_LAYOUT_GENERAL,
                .dst_stage_mask = rhi::STAGE_TRANSFER_BIT,
                .dst_access_mask = rhi::ACCESS_TRANSFER_WRITE_BIT,
                .mip_count = mip_levels,
            }}
        );

        rhi::cmd_copy_buffer_to_texture(
            cb,
            staging_buffer,
            m_textures[i],
            {copy_regions.data(), copy_regions.size()}
        );

        rhi::end_command_buffer(cb);

        rhi::queue_submit(m_transfer_queue, {{.cb = cb}});
        rhi::queue_wait_idle(m_transfer_queue);
    }

    for (size_t i = 0; i < m_textures.size(); ++i) {
        rhi::update_texture_descriptor_set(m_textures[i], u32(i));
    }

    return true;
}

void Renderer::update_shadow_cascades() {
    const Mat4 view_to_world = math::inverse(m_uniform_data.world_to_view);
    const Mat4 world_to_light =
        math::look_at(Vec3{0.0f}, m_uniform_data.sun_direction_world, WORLD_Y);

    // Calculate a combined view and orthographic projection matrix for each cascade.
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i) {
        // By using radius instead of min/max points of the frustum, we fix the projection size,
        // since a sphere can enclose any possible frustum orientation.
        // This helps with shadow map stability.
        const f32 sphere_radius = m_shadow_cascade_radii[i];
        const f32 sphere_diameter = sphere_radius * 2.0f;

        const Mat4 world_to_light_scaled =
            math::scale(world_to_light, RENDERER_SHADOW_MAP_DIMENSIONS / sphere_diameter);
        const Mat4 light_to_world_scaled = math::inverse(world_to_light_scaled);

        // TODO: push as far as it can be pushed without gaps.
        Vec4 sphere_center = {0.0f, 0.0f, -sphere_radius, 1.0f};
        sphere_center = view_to_world * sphere_center;
        // Texel snapping in light space to further stabilize shadow maps.
        sphere_center = world_to_light_scaled * sphere_center;
        sphere_center.x() = floorf(sphere_center.x());
        sphere_center.y() = floorf(sphere_center.y());
        sphere_center = light_to_world_scaled * sphere_center;

        const Mat4 light_view_matrix = math::look_at(
            sphere_center.xyz() - m_uniform_data.sun_direction_world * sphere_radius,
            sphere_center.xyz(),
            WORLD_Y
        );

        // (1, 1) is negative to flip y.
        // clang-format off
        const Mat4 light_projection_matrix = {
            1.0f / sphere_radius, 0.0f, 0.0f, 0.0f,
            0.0f, -1.0f / sphere_radius, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f / sphere_diameter, 0.0f,
            0.0f, 0.0f, 1.0f, 1.0f
        };
        // clang-format on

        m_shadow_pass_data.texel_sizes[i] = sphere_diameter / RENDERER_SHADOW_MAP_DIMENSIONS;
        m_shadow_pass_data.world_to_clip[i] = light_projection_matrix * light_view_matrix;
    }
}

void Renderer::visibility_buffer_pass(rhi::CommandBuffer cb, bool cull_late) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_visibility_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            cull_late ? m_draw_indices_late_buffer : m_draw_indices_early_buffer,
            m_draw_data_buffer,
            m_vertex_buffer,
        }
    );

    const PushConstantsVisibilityBuffer push_constants = {
        .cull_late = cull_late,
    };
    rhi::cmd_push_constants(cb, pipeline, &push_constants);

    const Vec3U32 render_dim = rhi::get_texture_dimensions(m_render_texture);

    rhi::cmd_set_viewport({
        .cb = cb,
        .y = f32(render_dim.y),
        .width = f32(render_dim.x),
        .height = -f32(render_dim.y),
    });

    rhi::cmd_set_scissor({.cb = cb, .extent = render_dim.xy()});

    rhi::cmd_begin_rendering({
        .cb = cb,
        .extent = rhi::get_texture_dimensions(m_render_texture).xy(),
        .color_targets = {{
            .attachment = m_visibility_texture,
            .load_op = cull_late ? rhi::ATTACHMENT_LOAD_OP_LOAD : rhi::ATTACHMENT_LOAD_OP_CLEAR,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        }},
        .depth_target = {
            .attachment = m_depth_texture,
            .load_op = cull_late ? rhi::ATTACHMENT_LOAD_OP_LOAD : rhi::ATTACHMENT_LOAD_OP_CLEAR,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        },
    });

    rhi::cmd_bind_index_buffer(cb, m_index_buffer, 0, rhi::INDEX_TYPE_U32);

    rhi::cmd_draw_indexed_indirect_count(
        cb,
        cull_late ? m_draw_cmd_late_buffer2 : m_draw_cmd_early_buffer2,
        0,
        m_draw_count_buffer,
        0,
        m_uniform_data.draw_count
    );

    rhi::cmd_end_rendering(cb);
}

void Renderer::cull_pass(rhi::CommandBuffer cb, bool late) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = late ? m_cull_late_pipeline : m_cull_early_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_constants(cb, pipeline, &m_cull_pass_data);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            m_draw_data_buffer,
            m_draw_count_buffer,
            m_draw_cmd_buffer1,
            late ? m_draw_cmd_late_buffer2 : m_draw_cmd_early_buffer2,
            late ? m_draw_indices_late_buffer : m_draw_indices_early_buffer,
            m_mesh_primitive_visible_buffer,
            m_min_sampler,
            // TODO: this is so fucking hacky, early pass doesn't use depth pyramid,
            // early/late pipelines are created with different specialization constants,
            // but this binding exists in both and must be filled correctly, for now
            // just using a dummy texture for the early pass.
            late ? m_depth_pyramid_texture : m_textures[0],
            m_debug_draw_count_buffer,
            m_debug_draw_rect_buffer,
        }
    );

    rhi::cmd_dispatch(
        cb,
        {
            calc_dispatch_size(m_uniform_data.draw_count, rhi::get_pipeline_local_size(pipeline).x),
            1,
            1,
        }
    );
}

void Renderer::depth_reduce_pass(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_depth_reduce_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    PushConstantsDepthReduce push_constants{};

    // In, out.
    rhi::DescriptorInfo descInfos[] = {
        m_depth_texture,
        m_depth_pyramid_mip_texture_descriptors[0],
        m_min_sampler,
    };

    const Vec2U32 depth_pyramid_size = rhi::get_texture_dimensions(m_depth_pyramid_texture).xy();

    for (size_t i = 0; i < m_depth_pyramid_mip_texture_descriptors.size(); ++i) {
        descInfos[1].resource.texture_descriptor = m_depth_pyramid_mip_texture_descriptors[i];

        const u32 width = math::max(1U, depth_pyramid_size.x >> i);
        const u32 height = math::max(1U, depth_pyramid_size.y >> i);

        push_constants.out_width = width;
        push_constants.out_height = height;
        push_constants.mip_level = u32(i);

        rhi::cmd_push_constants(cb, pipeline, &push_constants);

        rhi::cmd_push_descriptors(cb, pipeline, descInfos);

        rhi::cmd_dispatch(
            cb,
            calc_dispatch_size({width, height}, rhi::get_pipeline_local_size(pipeline))
        );

        rhi::cmd_barrier(
            cb,
            rhi::STAGE_COMPUTE_SHADER_BIT,
            rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
            rhi::STAGE_COMPUTE_SHADER_BIT,
            rhi::ACCESS_SHADER_SAMPLED_READ_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
        );

        descInfos[0].type = rhi::DescriptorInfo::TYPE_TEXTURE_DESCRIPTOR;
        descInfos[0].resource.texture_descriptor = m_depth_pyramid_mip_texture_descriptors[i];
    }
}

void Renderer::depth_view_quarter_res_pass(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_depth_view_quarter_res_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_constants(cb, pipeline, &m_ssao_pass_data);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_depth_texture,
            m_depth_view_quarter_res_texture,
        }
    );

    cmd_dispatch_over_texture_size(cb, pipeline, m_depth_view_quarter_res_texture);
}

void Renderer::ambient_occlusion_pass(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_ambient_occlusion_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_constants(cb, pipeline, &m_ssao_pass_data);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            m_draw_indices_early_buffer,
            m_draw_indices_late_buffer,
            m_draw_cmd_early_buffer2,
            m_draw_cmd_late_buffer2,
            m_draw_data_buffer,
            m_index_buffer,
            m_vertex_buffer,
            m_nearest_sampler,
            m_depth_view_quarter_res_texture,
            m_visibility_texture,
            m_ambient_occlusion_texture,
        }
    );

    cmd_dispatch_over_texture_size(cb, pipeline, m_ambient_occlusion_texture);
}

void Renderer::shadow_cull_pass(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_shadow_cull_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    const PushConstantsShadow push_constants = {
        .shadow_cascade_idx = 0,
        .render_pass_flags = RENDER_PASS_OPAQUE_BIT,
    };
    rhi::cmd_push_constants(cb, pipeline, &push_constants);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            m_draw_data_buffer,
            m_draw_count_buffer,
            m_draw_cmd_buffer1,
            m_draw_cmd_shadow_buffer,
            m_draw_indices_shadow_buffer,
        }
    );

    rhi::cmd_dispatch(
        cb,
        {calc_dispatch_size(m_uniform_data.draw_count, RENDERER_CULL_WORKGROUP_SIZE), 1, 1}
    );
}

void Renderer::shadow_pass(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_shadow_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            m_shadow_pass_uniform_buffer_descriptor,
            m_draw_indices_shadow_buffer,
            m_draw_data_buffer,
            m_vertex_buffer,
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

    rhi::cmd_bind_index_buffer(cb, m_index_buffer, 0, rhi::INDEX_TYPE_U32);

    // TODO: check out VK_KHR_multiview.
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i) {
        const PushConstantsShadow pushConstants = {
            .shadow_cascade_idx = i,
            .render_pass_flags = RENDER_PASS_OPAQUE_BIT,
        };
        rhi::cmd_push_constants(cb, m_shadow_cull_pipeline, &pushConstants);

        rhi::cmd_begin_rendering({
            .cb = cb,
            .extent = {RENDERER_SHADOW_MAP_DIMENSIONS, RENDERER_SHADOW_MAP_DIMENSIONS},
            .depth_target = {
                .attachment = m_shadow_texture_descriptor_cascade[i],
                .load_op = rhi::ATTACHMENT_LOAD_OP_CLEAR,
                .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
            },
        });

        rhi::cmd_draw_indexed_indirect_count(
            cb,
            m_draw_cmd_shadow_buffer,
            0,
            m_draw_count_buffer,
            0,
            m_uniform_data.draw_count
        );

        rhi::cmd_end_rendering(cb);
    }
}

void Renderer::fog_pass(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_fog_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            m_shadow_pass_uniform_buffer_descriptor,
            m_nearest_sampler,
            m_depth_texture,
            m_shadow_texture,
            m_fog_texture,
        }
    );

    cmd_dispatch_over_texture_size(cb, pipeline, m_fog_texture);
}

void Renderer::blur_fog_pass(rhi::CommandBuffer cb, bool horizontal) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_blur_fog_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            horizontal ? m_fog_texture : m_fog_blurred_horizontal_texture,
            horizontal ? m_fog_blurred_horizontal_texture : m_fog_blurred_vertical_texture,
        }
    );

    const PushConstantsFogBlur push_constants = {.horizontal = horizontal};
    rhi::cmd_push_constants(cb, pipeline, &push_constants);

    cmd_dispatch_over_texture_size(cb, pipeline, m_fog_blurred_horizontal_texture);
}

void Renderer::ambient_occlusion_blur_pass(rhi::CommandBuffer cb, bool horizontal) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_ambient_occlusion_blur_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_constants(cb, pipeline, &m_ssao_pass_data);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_depth_view_quarter_res_texture,
            horizontal ? m_ambient_occlusion_texture
                       : m_ambient_occlusion_blurred_horizontal_texture,
            horizontal ? m_ambient_occlusion_blurred_horizontal_texture
                       : m_ambient_occlusion_blurred_vertical_texture,
        }
    );

    m_ssao_pass_data.pixel_offset_x = horizontal ? 1 : 0;
    m_ssao_pass_data.pixel_offset_y = horizontal ? 0 : 1;
    rhi::cmd_push_constants(cb, pipeline, &m_ssao_pass_data);

    cmd_dispatch_over_texture_size(cb, pipeline, m_ambient_occlusion_blurred_horizontal_texture);
}

void Renderer::ambient_occlusion_upsample_pass(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_ambient_occlusion_upsample_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            m_nearest_sampler,
            m_depth_texture,
            m_depth_view_quarter_res_texture,
            m_ambient_occlusion_blurred_vertical_texture,
            m_ambient_occlusion_upsampled_texture,
        }
    );

    cmd_dispatch_over_texture_size(cb, pipeline, m_ambient_occlusion_upsampled_texture);
}

void Renderer::render_pass(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_visibility_render_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            m_shadow_pass_uniform_buffer_descriptor,
            m_draw_indices_early_buffer,
            m_draw_indices_late_buffer,
            m_draw_cmd_early_buffer2,
            m_draw_cmd_late_buffer2,
            m_draw_data_buffer,
            m_index_buffer,
            m_vertex_buffer,
            m_material_buffer,
            m_linear_sampler,
            m_texture_sampler,
            m_shadow_sampler,
            m_shadow_pcf_jitter_sampler,
            m_shadow_texture,
            m_shadow_pcf_jitter_texture,
            m_fog_blurred_vertical_texture,
            m_visibility_texture,
            m_ambient_occlusion_upsampled_texture,
            m_velocity_texture,
            m_render_texture,
        }
    );

    rhi::cmd_bind_texture_descriptor_set(cb, pipeline);

    cmd_dispatch_over_texture_size(cb, pipeline, m_render_texture);
}

void Renderer::taa_resolve_pass(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    const rhi::Pipeline pipeline = m_taa_resolve_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_constants(cb, pipeline, &m_taa_pass_data);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            m_render_texture,
            m_depth_texture,
            m_velocity_texture,
            m_swapchain_recreated || m_render_mode_changed
                ? m_frame[m_frame_idx].resolved_render_texture
                : m_frame[m_prev_frame_idx].resolved_render_texture,
            m_frame[m_frame_idx].resolved_render_texture,
            m_linear_sampler,
        }
    );

    cmd_dispatch_over_texture_size(cb, pipeline, m_frame[m_frame_idx].resolved_render_texture);
}

void Renderer::debug_draw_pass(rhi::CommandBuffer cb) {
    DEBUG_ASSERT(cb);

    rhi::cmd_bind_pipeline(cb, m_debug_draw_fill_cmd_pipeline);

    rhi::cmd_push_descriptors(
        cb,
        m_debug_draw_fill_cmd_pipeline,
        {
            m_debug_draw_count_buffer,
            m_debug_draw_cmd_buffer,
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

    rhi::cmd_bind_pipeline(cb, m_debug_draw_rect_pipeline);

    rhi::cmd_push_descriptors(
        cb,
        m_debug_draw_rect_pipeline,
        {
            m_uniform_buffer_descriptor,
            m_debug_draw_count_buffer,
            m_debug_draw_rect_buffer,
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
            .attachment = m_frame[m_frame_idx].resolved_render_texture,
            .load_op = rhi::ATTACHMENT_LOAD_OP_LOAD,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        }},
    });

    rhi::cmd_draw_indirect(cb, m_debug_draw_cmd_buffer, 0, 1, 0);

    rhi::cmd_end_rendering(cb);
}

void Renderer::fullscreen_pass(rhi::CommandBuffer cb, rhi::Texture swapchain_texture) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(swapchain_texture);

    const rhi::Pipeline pipeline = m_fullscreen_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_frame[m_frame_idx].resolved_render_texture,
            m_linear_sampler,
        }
    );

    const Vec2U32 size = rhi::get_texture_dimensions(swapchain_texture).xy();

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
            .attachment = swapchain_texture,
            .load_op = rhi::ATTACHMENT_LOAD_OP_LOAD,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        }},
    });

    rhi::cmd_draw(cb, 3, 1, 0, 0);

    rhi::cmd_end_rendering(cb);
}

void Renderer::debug_draw_grad_error_pass(
    rhi::CommandBuffer cb,
    bool cull_late,
    rhi::Texture swapchain_texture
) {
    DEBUG_ASSERT(cb);
    DEBUG_ASSERT(swapchain_texture);

    const rhi::Pipeline pipeline = m_debug_grad_error_pipeline;

    rhi::cmd_bind_pipeline(cb, pipeline);

    rhi::cmd_push_descriptors(
        cb,
        pipeline,
        {
            m_uniform_buffer_descriptor,
            cull_late ? m_draw_indices_late_buffer : m_draw_indices_early_buffer,
            cull_late ? m_draw_cmd_late_buffer2 : m_draw_cmd_early_buffer2,
            m_draw_data_buffer,
            m_index_buffer,
            m_vertex_buffer,
        }
    );

    const Vec2U32 size = rhi::get_texture_dimensions(swapchain_texture).xy();

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
            .attachment = swapchain_texture,
            .load_op = cull_late ? rhi::ATTACHMENT_LOAD_OP_LOAD : rhi::ATTACHMENT_LOAD_OP_CLEAR,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        }},
        .depth_target = {
            .attachment = m_depth_texture,
            .load_op = cull_late ? rhi::ATTACHMENT_LOAD_OP_LOAD : rhi::ATTACHMENT_LOAD_OP_CLEAR,
            .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
        },
    });

    rhi::cmd_bind_index_buffer(cb, m_index_buffer, 0, rhi::INDEX_TYPE_U32);

    rhi::cmd_draw_indexed_indirect_count(
        cb,
        cull_late ? m_draw_cmd_late_buffer2 : m_draw_cmd_early_buffer2,
        0,
        m_draw_count_buffer,
        0,
        m_uniform_data.draw_count
    );

    rhi::cmd_end_rendering(cb);
}

void Renderer::record_and_submit_debug_grad_error(rhi::Texture swapchain_texture) {
    DEBUG_ASSERT(swapchain_texture);

    const rhi::CommandBuffer cb = m_frame[m_frame_idx].start_command_buffer;

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
            m_visibility_texture,
            m_depth_pyramid_texture,
            swapchain_texture,
            m_depth_texture,
        }
    );

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_DRAW_INDIRECT_BIT,
        rhi::ACCESS_NONE,
        rhi::STAGE_TRANSFER_BIT,
        rhi::ACCESS_NONE
    );

    rhi::cmd_fill_buffer(cb, m_draw_count_buffer, 0, sizeof(u32), 0);

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_TRANSFER_BIT | rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_FRAGMENT_SHADER_BIT,
        rhi::ACCESS_TRANSFER_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    cull_pass(cb, false);

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_DRAW_INDIRECT_BIT | rhi::STAGE_VERTEX_SHADER_BIT
            | rhi::STAGE_FRAGMENT_SHADER_BIT,
        rhi::ACCESS_INDIRECT_COMMAND_READ_BIT | rhi::ACCESS_SHADER_STORAGE_READ_BIT
    );

    debug_draw_grad_error_pass(cb, false, swapchain_texture);

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_LATE_FRAGMENT_TESTS_BIT,
        rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    if (!m_cull_camera_frozen) {
        depth_reduce_pass(cb);
    }

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_DRAW_INDIRECT_BIT,
        rhi::ACCESS_NONE,
        rhi::STAGE_COMPUTE_SHADER_BIT | rhi::STAGE_TRANSFER_BIT,
        rhi::ACCESS_NONE
    );

    rhi::cmd_fill_buffer(cb, m_draw_count_buffer, 0, sizeof(u32), 0);

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_TRANSFER_BIT | rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_FRAGMENT_SHADER_BIT
            | rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_TRANSFER_WRITE_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_SHADER_SAMPLED_READ_BIT
            | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    cull_pass(cb, true);

    rhi::cmd_barrier(
        cb,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_DRAW_INDIRECT_BIT | rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_FRAGMENT_SHADER_BIT
            | rhi::STAGE_COMPUTE_SHADER_BIT | rhi::STAGE_EARLY_FRAGMENT_TESTS_BIT,
        rhi::ACCESS_INDIRECT_COMMAND_READ_BIT | rhi::ACCESS_SHADER_STORAGE_READ_BIT
            | rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
    );

    debug_draw_grad_error_pass(cb, true, swapchain_texture);

    m_imgui_renderer.update_vertex_index_buffers(u32(m_frame_idx));

    // TODO: separate pass.
    if (m_enable_ui) {
        const Vec2U32 size = rhi::get_texture_dimensions(swapchain_texture).xy();

        rhi::cmd_begin_rendering({
            .cb = cb,
            .extent = size,
            .color_targets = {{
                .attachment = swapchain_texture,
                .load_op = rhi::ATTACHMENT_LOAD_OP_LOAD,
                .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
            }},
        });

        m_imgui_renderer.render(cb, u32(m_frame_idx));

        rhi::cmd_end_rendering(cb);
    }

    rhi::cmd_texture_barrier(
        cb,
        {{
            swapchain_texture,
            rhi::TEXTURE_LAYOUT_GENERAL,
            rhi::TEXTURE_LAYOUT_PRESENT_SRC,
            rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            rhi::ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            rhi::STAGE_NONE,
            rhi::ACCESS_NONE,
        }}
    );

    rhi::end_command_buffer(cb);

    m_frame[m_frame_idx].uniform_buffer_manager.flush();
    rhi::queue_submit(
        m_graphics_queue,
        {{
            .cb = cb,
            .wait_for_texture_acquire = true,
            .signal_ready_to_present = true,
            .signal_semaphores = {
                {m_frame_semaphore, m_uniform_data.frame_count + 1},
            },
        }}
    );
}

void Renderer::record_and_submit_visibility(rhi::Texture swapchain_texture) {
    Frame& frame = m_frame[m_frame_idx];

    const rhi::CommandBuffer cb_start = frame.start_command_buffer;
    rhi::begin_command_buffer(cb_start);

    rhi::cmd_texture_invalidate_barrier(
        cb_start,
        rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        rhi::ACCESS_NONE,
        rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | rhi::STAGE_EARLY_FRAGMENT_TESTS_BIT
            | rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_COLOR_ATTACHMENT_WRITE_BIT | rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        {
            m_visibility_texture,
            m_depth_pyramid_texture,
            m_ambient_occlusion_texture,
            m_depth_view_quarter_res_texture,
            m_ambient_occlusion_blurred_horizontal_texture,
            m_ambient_occlusion_blurred_vertical_texture,
            m_ambient_occlusion_upsampled_texture,
            m_render_texture,
            m_velocity_texture,
            m_fog_texture,
            m_fog_blurred_horizontal_texture,
            m_fog_blurred_vertical_texture,
            frame.resolved_render_texture,
            swapchain_texture,
            m_depth_texture,
            m_shadow_texture,
        }
    );

    rhi::cmd_fill_buffer(cb_start, m_draw_count_buffer, 0, sizeof(u32), 0);
    rhi::cmd_fill_buffer(cb_start, m_debug_draw_count_buffer, 0, sizeof(u32), 0);

    rhi::cmd_barrier(
        cb_start,
        rhi::STAGE_TRANSFER_BIT,
        rhi::ACCESS_TRANSFER_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    cull_pass(cb_start, false);

    rhi::cmd_barrier(
        cb_start,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_DRAW_INDIRECT_BIT | rhi::STAGE_VERTEX_SHADER_BIT,
        rhi::ACCESS_INDIRECT_COMMAND_READ_BIT | rhi::ACCESS_SHADER_STORAGE_READ_BIT
    );

    visibility_buffer_pass(cb_start, false);

    rhi::cmd_barrier(
        cb_start,
        rhi::STAGE_LATE_FRAGMENT_TESTS_BIT,
        rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    if (!m_cull_camera_frozen) {
        depth_reduce_pass(cb_start);
    }

    rhi::cmd_barrier(
        cb_start,
        rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_DRAW_INDIRECT_BIT,
        rhi::ACCESS_NONE,
        rhi::STAGE_COMPUTE_SHADER_BIT | rhi::STAGE_TRANSFER_BIT,
        rhi::ACCESS_NONE
    );

    rhi::cmd_fill_buffer(cb_start, m_draw_count_buffer, 0, sizeof(u32), 0);

    rhi::cmd_barrier(
        cb_start,
        rhi::STAGE_TRANSFER_BIT | rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_FRAGMENT_SHADER_BIT
            | rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_TRANSFER_WRITE_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_SHADER_SAMPLED_READ_BIT
            | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    cull_pass(cb_start, true);

    rhi::cmd_barrier(
        cb_start,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_DRAW_INDIRECT_BIT | rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_COMPUTE_SHADER_BIT
            | rhi::STAGE_EARLY_FRAGMENT_TESTS_BIT,
        rhi::ACCESS_INDIRECT_COMMAND_READ_BIT | rhi::ACCESS_SHADER_STORAGE_READ_BIT
            | rhi::ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
    );

    visibility_buffer_pass(cb_start, true);

    rhi::end_command_buffer(cb_start);

    // TODO: I don't see why can't I use 2 submits per frame (1 for each queue).
    frame.uniform_buffer_manager.flush();
    rhi::queue_submit(
        m_graphics_queue,
        {{
            .cb = cb_start,
            .wait_for_texture_acquire = true,
            .signal_semaphores = {
                {frame.start_semaphore.semaphore, frame.start_semaphore.inc()},
            },
        }}
    );

    const rhi::CommandBuffer cb_ssao = frame.ssao_command_buffer;
    rhi::begin_command_buffer(cb_ssao);

    depth_view_quarter_res_pass(cb_ssao);

    rhi::cmd_barrier(
        cb_ssao,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    ambient_occlusion_pass(cb_ssao);

    rhi::cmd_barrier(
        cb_ssao,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    ambient_occlusion_blur_pass(cb_ssao, true);

    rhi::cmd_barrier(
        cb_ssao,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    ambient_occlusion_blur_pass(cb_ssao, false);

    rhi::cmd_barrier(
        cb_ssao,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    ambient_occlusion_upsample_pass(cb_ssao);

    rhi::end_command_buffer(cb_ssao);

    frame.uniform_buffer_manager.flush();
    rhi::queue_submit(
        m_compute_queue,
        {{
            .cb = cb_ssao,
            .wait_semaphores =
                {
                    {frame.start_semaphore.semaphore, frame.start_semaphore.value},
                },
            .signal_semaphores = {
                {frame.ssao_semaphore.semaphore, frame.ssao_semaphore.inc()},
            },
        }}
    );

    const rhi::CommandBuffer cb_shadow = frame.shadow_command_buffer;
    rhi::begin_command_buffer(cb_shadow);

    rhi::cmd_fill_buffer(cb_shadow, m_draw_count_buffer, 0, sizeof(u32), 0);

    rhi::cmd_barrier(
        cb_shadow,
        rhi::STAGE_TRANSFER_BIT,
        rhi::ACCESS_TRANSFER_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_SHADER_STORAGE_WRITE_BIT
    );

    shadow_cull_pass(cb_shadow);

    rhi::cmd_barrier(
        cb_shadow,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_VERTEX_SHADER_BIT | rhi::STAGE_DRAW_INDIRECT_BIT,
        rhi::ACCESS_SHADER_STORAGE_READ_BIT | rhi::ACCESS_INDIRECT_COMMAND_READ_BIT
    );

    shadow_pass(cb_shadow);

    rhi::end_command_buffer(cb_shadow);

    frame.uniform_buffer_manager.flush();
    rhi::queue_submit(
        m_graphics_queue,
        {{
            .cb = cb_shadow,
            .wait_semaphores =
                {
                    {frame.start_semaphore.semaphore, frame.start_semaphore.value},
                },
            .signal_semaphores = {
                {frame.shadow_semaphore.semaphore, frame.shadow_semaphore.inc()},
            },
        }}
    );

    const rhi::CommandBuffer cb_end = frame.end_command_buffer;
    rhi::begin_command_buffer(cb_end);

    fog_pass(cb_end);

    rhi::cmd_barrier(
        cb_end,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    blur_fog_pass(cb_end, true);

    rhi::cmd_barrier(
        cb_end,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    blur_fog_pass(cb_end, false);

    rhi::cmd_barrier(
        cb_end,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    render_pass(cb_end);

    rhi::cmd_barrier(
        cb_end,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    taa_resolve_pass(cb_end);

    rhi::cmd_barrier(
        cb_end,
        rhi::STAGE_COMPUTE_SHADER_BIT,
        rhi::ACCESS_SHADER_STORAGE_WRITE_BIT,
        rhi::STAGE_FRAGMENT_SHADER_BIT | rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT | rhi::ACCESS_COLOR_ATTACHMENT_READ_BIT
    );

    debug_draw_pass(cb_end);

    rhi::cmd_barrier(
        cb_end,
        rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        rhi::ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        rhi::STAGE_FRAGMENT_SHADER_BIT,
        rhi::ACCESS_SHADER_SAMPLED_READ_BIT
    );

    fullscreen_pass(cb_end, swapchain_texture);

    m_imgui_renderer.update_vertex_index_buffers(u32(m_frame_idx));

    if (m_enable_ui) {
        const Vec2U32 size = rhi::get_texture_dimensions(swapchain_texture).xy();

        rhi::cmd_begin_rendering({
            .cb = cb_end,
            .extent = size,
            .color_targets = {{
                .attachment = swapchain_texture,
                .load_op = rhi::ATTACHMENT_LOAD_OP_LOAD,
                .store_op = rhi::ATTACHMENT_STORE_OP_STORE,
            }},
        });

        m_imgui_renderer.render(cb_end, u32(m_frame_idx));

        rhi::cmd_end_rendering(cb_end);
    }

    rhi::cmd_texture_barrier(
        cb_end,
        {{
            swapchain_texture,
            rhi::TEXTURE_LAYOUT_GENERAL,
            rhi::TEXTURE_LAYOUT_PRESENT_SRC,
            rhi::STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            rhi::ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            rhi::STAGE_NONE,
            rhi::ACCESS_NONE,
        }}
    );

    rhi::end_command_buffer(cb_end);

    frame.uniform_buffer_manager.flush();
    rhi::queue_submit(
        m_graphics_queue,
        {{
            .cb = cb_end,
            .wait_semaphores =
                {
                    {frame.ssao_semaphore.semaphore, frame.ssao_semaphore.value},
                    {frame.shadow_semaphore.semaphore, frame.shadow_semaphore.value},
                },
            .signal_ready_to_present = true,
            .signal_semaphores = {
                {m_frame_semaphore, m_uniform_data.frame_count + 1},
            },
        }}
    );
}

void Renderer::create_swapchain(Vec2U32 size) {
    rhi::device_wait_idle();

    cleanup_swapchain();
    cleanup_color_resources();
    cleanup_depth_resources();

    rhi::create_swapchain(size);

    m_uniform_data.swapchain_width = size.x;
    m_uniform_data.swapchain_height = size.y;
    m_uniform_data.view_to_clip =
        math::perspective(FOV_Y_RAD, f32(size.x) / f32(size.y), RENDERER_NEAR_PLANE);
    m_uniform_data.view_to_clip_inv0011 = {
        1.0f / m_uniform_data.view_to_clip(0, 0),
        1.0f / m_uniform_data.view_to_clip(1, 1),
    };
    m_uniform_data.prev_world_to_clip = m_uniform_data.world_to_clip;
    m_uniform_data.world_to_clip = m_uniform_data.view_to_clip * m_uniform_data.world_to_view;
    m_uniform_data.clip_to_world = math::inverse(m_uniform_data.world_to_clip);

    create_color_resources();
    create_depth_resources();

    m_swapchain_recreated = true;
}

void Renderer::cleanup_swapchain() {
    rhi::destroy_swapchain();
}

void Renderer::create_color_resources() {
    const Vec2U32 swapchain_size = rhi::get_texture_dimensions(rhi::get_swapchain_texture(0)).xy();
    const Vec3U32 render_dimensions =
        {swapchain_size.x * RENDER_SCALE, swapchain_size.y * RENDER_SCALE, 1};

    m_render_texture = rhi::create_texture({
        .format = rhi::FORMAT_B10G11R11_UFLOAT_PACK32,
        .dimensions = render_dimensions,
        .usage = rhi::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | rhi::TEXTURE_USAGE_STORAGE_BIT
            | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "render_texture",
    });

    m_uniform_data.render_width = render_dimensions.x;
    m_uniform_data.render_height = render_dimensions.y;
    m_uniform_data.aspect = f32(m_uniform_data.render_width) / f32(m_uniform_data.render_height);
    m_uniform_data.render_texture_size_inv = {
        1.0f / f32(render_dimensions.x),
        1.0f / f32(render_dimensions.y)
    };

    m_visibility_texture = rhi::create_texture({
        .format = rhi::FORMAT_R32G32_UINT,
        .dimensions = render_dimensions,
        .usage = rhi::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "visibility_texture",
    });

    const Vec3U32 ao_dimensions = {render_dimensions.x / 2, render_dimensions.y / 2, 1};

    m_ssao_pass_data.width = ao_dimensions.x;
    m_ssao_pass_data.height = ao_dimensions.y;
    m_ssao_pass_data.texture_size_inv = {1.0f / f32(ao_dimensions.x), 1.0f / f32(ao_dimensions.y)};

    m_ambient_occlusion_texture = rhi::create_texture({
        .format = rhi::FORMAT_R8_UNORM,
        .dimensions = ao_dimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "ambient_occlusion_texture",
    });

    m_ambient_occlusion_blurred_horizontal_texture = rhi::create_texture({
        .format = rhi::get_texture_format(m_ambient_occlusion_texture),
        .dimensions = ao_dimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "ambient_occlusion_blurred_horizontal_texture",
    });

    m_ambient_occlusion_blurred_vertical_texture = rhi::create_texture({
        .format = rhi::get_texture_format(m_ambient_occlusion_texture),
        .dimensions = ao_dimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "ambient_occlusion_blurred_vertical_texture",
    });

    m_ambient_occlusion_upsampled_texture = rhi::create_texture({
        .format = rhi::get_texture_format(m_ambient_occlusion_texture),
        .dimensions = render_dimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "ambient_occlusion_blurred_upsampled_texture",
    });

    m_fog_texture = rhi::create_texture({
        .format = rhi::FORMAT_R16_SFLOAT,
        .dimensions = render_dimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "fog_texture",
    });

    m_fog_blurred_horizontal_texture = rhi::create_texture({
        .format = rhi::get_texture_format(m_fog_texture),
        .dimensions = render_dimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "fog_blurred_horizontal_texture",
    });

    m_fog_blurred_vertical_texture = rhi::create_texture({
        .format = rhi::get_texture_format(m_fog_texture),
        .dimensions = render_dimensions,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "fog_blurred_vertical_texture",
    });

    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i) {
        m_frame[i].resolved_render_texture = rhi::create_texture({
            .format = rhi::get_texture_format(m_render_texture),
            .dimensions = render_dimensions,
            .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT
                | rhi::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT,
            .debug_name = "resolved_render_texture",
        });
    }

    m_velocity_texture = rhi::create_texture({
        .format = rhi::FORMAT_R16G16_SFLOAT,
        .dimensions = render_dimensions,
        .usage = rhi::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | rhi::TEXTURE_USAGE_STORAGE_BIT
            | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "velocity_texture",
    });
}

void Renderer::cleanup_color_resources() {
    for (int i = 0; i < rhi::FRAMES_IN_FLIGHT; ++i) {
        rhi::destroy_texture(m_frame[i].resolved_render_texture);
    }
    rhi::destroy_texture(m_fog_blurred_vertical_texture);
    rhi::destroy_texture(m_fog_blurred_horizontal_texture);
    rhi::destroy_texture(m_fog_texture);
    rhi::destroy_texture(m_ambient_occlusion_upsampled_texture);
    rhi::destroy_texture(m_ambient_occlusion_blurred_vertical_texture);
    rhi::destroy_texture(m_ambient_occlusion_blurred_horizontal_texture);
    rhi::destroy_texture(m_ambient_occlusion_texture);
    rhi::destroy_texture(m_visibility_texture);
    rhi::destroy_texture(m_render_texture);
    rhi::destroy_texture(m_velocity_texture);
}

void Renderer::create_depth_resources() {
    const Vec3U32 render_dimensions = rhi::get_texture_dimensions(m_render_texture);

    m_depth_texture = rhi::create_texture({
        .format = rhi::FORMAT_D32_SFLOAT,
        .dimensions = render_dimensions,
        .usage = rhi::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "depth_texture",
    });

    m_depth_view_quarter_res_texture = rhi::create_texture({
        .format = rhi::FORMAT_R16_SFLOAT,
        .dimensions = {render_dimensions.x / 2, render_dimensions.y / 2, 1},
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "depth_view_quarter_res_texture",
    });

    const Vec2U32 depth_pyramid_size = {
        math::previous_pow2(render_dimensions.x),
        math::previous_pow2(render_dimensions.y)
    };
    const u32 depth_pyramid_mip_levels =
        utils::get_mip_level(depth_pyramid_size.x, depth_pyramid_size.y);

    // PreviousPow2 to make reductions at most by 2x2, otherwise they are not conservative.
    m_depth_pyramid_texture = rhi::create_texture({
        .format = rhi::FORMAT_R32_SFLOAT,
        .dimensions = {depth_pyramid_size.x, depth_pyramid_size.y, 1},
        .mip_count = depth_pyramid_mip_levels,
        .usage = rhi::TEXTURE_USAGE_STORAGE_BIT | rhi::TEXTURE_USAGE_SAMPLED_BIT,
        .debug_name = "depth_pyramid_texture",
    });

    m_cull_pass_data.depth_pyramid_width = f32(depth_pyramid_size.x);
    m_cull_pass_data.depth_pyramid_height = f32(depth_pyramid_size.y);

    m_depth_pyramid_mip_texture_descriptors.resize(depth_pyramid_mip_levels);

    for (size_t i = 0; i < m_depth_pyramid_mip_texture_descriptors.size(); ++i) {
        m_depth_pyramid_mip_texture_descriptors[i] = rhi::create_texture_descriptor({
            .texture = m_depth_pyramid_texture,
            .type = rhi::TEXTURE_TYPE_2D,
            .base_mip = u32(i),
            .mip_count = 1,
            .layer_count = 1,
        });
    }
}

void Renderer::cleanup_depth_resources() {
    rhi::destroy_texture(m_depth_texture);
    rhi::destroy_texture(m_depth_view_quarter_res_texture);
    rhi::destroy_texture(m_depth_pyramid_texture);
    for (rhi::TextureDescriptor h: m_depth_pyramid_mip_texture_descriptors) {
        rhi::destroy_texture_descriptor(h);
    }
}
