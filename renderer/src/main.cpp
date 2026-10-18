#include "camera.hpp"
#include "common.hpp"
#include "math/vec3.hpp"
#include "renderer/renderer.hpp"
#include "time_meter.hpp"
#include "utils.hpp"
#include "temp_allocator.hpp"

#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>

#include <stdio.h>

static u8 s_keys[SDL_SCANCODE_COUNT + 1];
static Camera s_camera;
bool s_should_window_close = false;
static bool s_is_mouse_relative_mode = true;
static bool s_need_update_view_matrix = true;
static bool s_fullscreen = true;
static bool s_cull_camera_frozen = false;
static RenderMode s_render_mode = RENDER_MODE_VISIBILITY;

static bool save_camera(const Camera& camera, const char* path) {
    DEBUG_ASSERT(path);

    FILE* const fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "%s: fopen %s failed: %s\n", __func__, path, strerror(errno));
        return false;
    }
    DEFER(fclose(fp));

    // TODO: very hacky but should be ok for POD.
    if (fwrite(&camera, sizeof(camera), 1, fp) != 1) {
        fprintf(stderr, "%s: fwrite %s failed: %s\n", __func__, path, strerror(errno));
        return false;
    }

    return true;
}

static bool load_camera(Camera& camera, SDL_Window* window, const char* path) {
    DEBUG_ASSERT(window);
    DEBUG_ASSERT(path);

    FILE* const fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "%s: fopen %s failed: %s\n", __func__, path, strerror(errno));
        return false;
    }
    DEFER(fclose(fp));

    Camera new_camera{};
    // TODO: very hacky but should be ok for POD.
    if (fread(&new_camera, sizeof(new_camera), 1, fp) != 1) {
        if (feof(fp)) {
            fprintf(stderr, "%s: fread %s failed: EOF\n", __func__, path);
        } else if (ferror(fp)) {
            fprintf(stderr, "%s: fread %s failed: %s\n", __func__, path, strerror(errno));
        }
        return false;
    }

    memcpy(&camera, &new_camera, sizeof(camera));

    SDL_SetWindowRelativeMouseMode(window, true);
    s_is_mouse_relative_mode = true;
    s_camera.m_lock_direction = false;

    return true;
}

static void process_mouse(SDL_Window* window) {
    assert(window);

    f32 x{};
    f32 y{};
    (void)SDL_GetRelativeMouseState(&x, &y);

    static bool s_first = true;

    if (s_first) {
        s_first = false;
        return;
    }

    s_camera.change_direction(x, -y);

    if (s_is_mouse_relative_mode) {
        SDL_WarpMouseInWindow(window, 0.0f, 0.0f);
    }

    s_need_update_view_matrix = true;
}

static void process_event(SDL_Window* window, const SDL_Event& event, Renderer& renderer) {
    DEBUG_ASSERT(window);

    ImGui_ImplSDL3_ProcessEvent(&event);

    switch (event.type) {
        case SDL_EVENT_QUIT:
            s_should_window_close = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            s_keys[event.key.scancode] = 1;
            break;
        case SDL_EVENT_KEY_UP:
            s_keys[event.key.scancode] = 0;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            process_mouse(window);
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            renderer.pause_rendering(true);
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            renderer.pause_rendering(false);
            break;
    }
}

static void process_input(SDL_Window* window, f32 delta_time, Renderer& renderer) {
    DEBUG_ASSERT(window);

    if (s_keys[SDL_SCANCODE_W]) {
        s_camera.move(Camera::MoveDirection::FORWARD, delta_time);
        s_need_update_view_matrix = true;
    }
    if (s_keys[SDL_SCANCODE_S]) {
        s_camera.move(Camera::MoveDirection::BACKWARD, delta_time);
        s_need_update_view_matrix = true;
    }
    if (s_keys[SDL_SCANCODE_D]) {
        s_camera.move(Camera::MoveDirection::RIGHT, delta_time);
        s_need_update_view_matrix = true;
    }
    if (s_keys[SDL_SCANCODE_A]) {
        s_camera.move(Camera::MoveDirection::LEFT, delta_time);
        s_need_update_view_matrix = true;
    }
    if (s_keys[SDL_SCANCODE_Z]) {
        s_camera.move(Camera::MoveDirection::DOWN, delta_time);
        s_need_update_view_matrix = true;
    }
    if (s_keys[SDL_SCANCODE_X]) {
        s_camera.move(Camera::MoveDirection::UP, delta_time);
        s_need_update_view_matrix = true;
    }

    if (s_keys[SDL_SCANCODE_PERIOD]) {
        s_keys[SDL_SCANCODE_PERIOD] = 0;
        s_fullscreen ^= true;
        SDL_SetWindowFullscreen(window, s_fullscreen);
    }

    if (s_keys[SDL_SCANCODE_ESCAPE]) {
        s_keys[SDL_SCANCODE_ESCAPE] = 0;
        s_should_window_close = true;
    }

    if (s_keys[SDL_SCANCODE_M]) {
        s_keys[SDL_SCANCODE_M] = 0;
        if (s_is_mouse_relative_mode) {
            SDL_SetWindowRelativeMouseMode(window, false);
            s_is_mouse_relative_mode = false;
            s_camera.m_lock_direction = true;
        } else {
            SDL_SetWindowRelativeMouseMode(window, true);
            s_is_mouse_relative_mode = true;
            s_camera.m_lock_direction = false;
        }
    }

    if (s_keys[SDL_SCANCODE_U]) {
        s_keys[SDL_SCANCODE_U] = 0;
        renderer.m_enable_ui ^= true;
    }

    if (s_keys[SDL_SCANCODE_C]) {
        s_keys[SDL_SCANCODE_C] = 0;
        s_cull_camera_frozen ^= true;
        renderer.freeze_cull_camera(s_cull_camera_frozen);
    }

    if (s_keys[SDL_SCANCODE_R]) {
        s_keys[SDL_SCANCODE_R] = 0;
        if (!renderer.recompile_pipelines()) {
            fprintf(stderr, "renderer: pipeline recompilation failed\n");
        }
    }

    if (s_keys[SDL_SCANCODE_1]) {
        s_keys[SDL_SCANCODE_1] = 0;
        s_render_mode = RENDER_MODE_VISIBILITY;
    }

    if (s_keys[SDL_SCANCODE_2]) {
        s_keys[SDL_SCANCODE_2] = 0;
        s_render_mode = RENDER_MODE_GRAD_ERROR;
    }

    if (s_keys[SDL_SCANCODE_3]) {
        s_keys[SDL_SCANCODE_3] = 0;
        s_render_mode = RENDER_MODE_AMBIENT_OCCLUSION;
    }
}

static void imgui_checkbox(const char* label, u32& value) {
    bool bool_value = value;
    ImGui::Checkbox(label, &bool_value);
    value = bool_value;
}

static void dir_to_yaw_pitch_deg(f32& yaw, f32& pitch, Vec3 dir) {
    yaw = atan2f(dir.x(), dir.z());
    if (yaw < 0.0f) {
        yaw += 2.0f * M_PIf;
    }
    yaw = math::degrees(yaw);
    pitch = math::degrees(asinf(-dir.y()));
}

int main() {
    init_temp_allocator();

    Renderer renderer{};

    if (!renderer.init()) {
        fprintf(stderr, "renderer initialization failed\n");
        return 1;
    }
    DEFER(renderer.cleanup());

    bool enable_camera_loading = true;
    f32 sun_yaw = 0.0f;
    f32 sun_pitch = 0.0f;

    dir_to_yaw_pitch_deg(sun_yaw, sun_pitch, renderer.m_uniform_data.sun_direction_world);

    // TODO: hardcoded initial value (not from gltf) to showcase fog.
    sun_yaw = 185.4f;
    sun_pitch = 57.38f;

    renderer.m_enable_ui = true;
    renderer.m_taa_pass_data.enable = 1;
    renderer.change_render_mode(s_render_mode);

    s_camera.m_position = {9.4f, 7.4f, 0.8f};
    s_camera.m_yaw = math::radians(-85.0f);
    s_camera.m_pitch = math::radians(0.0f);
    s_camera.m_world_up = WORLD_Y;
    s_camera.m_speed = 10.0f;
    s_camera.m_mouse_sensitivity = 0.002f;
    s_camera.m_pitch_clamp = math::radians(89.0f);
    if (enable_camera_loading) {
        if (!load_camera(s_camera, renderer.m_window, "camera.bin")) {
            fprintf(stderr, "camera loading failed\n");
        }
    }
    s_camera.update_vectors();
    renderer.update_camera(s_camera.m_position, s_camera.get_view_matrix());

    u64 performance_counter = SDL_GetPerformanceCounter();
    const f64 performance_period = 1.0 / f64(SDL_GetPerformanceFrequency());
    u64 last_performance_counter = performance_counter;

    // To prevent a very big first measurement since m_start_time == 0
    // and it uses MeasureBetween function.
    g_time_meters[TimeMeter::FRAME].start();

    while (!s_should_window_close) {
        last_performance_counter = performance_counter;
        performance_counter = SDL_GetPerformanceCounter();
        const f64 delta_time =
            f64(performance_counter - last_performance_counter) * performance_period;

        g_time_meters[TimeMeter::FRAME].measure_between();

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            process_event(renderer.m_window, event, renderer);
        }
        process_input(renderer.m_window, f32(delta_time), renderer);

        renderer.change_render_mode(s_render_mode);

        (void)renderer.start_new_frame();

        ImGui::Begin("Main");

        if (ImGui::BeginTable("Info", 2)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SeparatorText("Camera");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Position");
            ImGui::TableNextColumn();
            ImGui::Text(
                "%.1f %.1f %.1f",
                s_camera.m_position.x(),
                s_camera.m_position.y(),
                s_camera.m_position.z()
            );

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Direction");
            ImGui::TableNextColumn();
            ImGui::Text(
                "%.1f %.1f %.1f",
                s_camera.m_direction.x(),
                s_camera.m_direction.y(),
                s_camera.m_direction.z()
            );

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Button("Save")) {
                if (!save_camera(s_camera, "camera.bin")) {
                    fprintf(stderr, "camera saving failed\n");
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                if (!load_camera(s_camera, renderer.m_window, "camera.bin")) {
                    fprintf(stderr, "camera loading failed\n");
                }
                s_camera.update_vectors();
                renderer.update_camera(s_camera.m_position, s_camera.get_view_matrix());
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SeparatorText("Misc");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SliderFloat(
                "##misc_sun_intensity",
                &renderer.m_uniform_data.sun_intensity,
                1.0f,
                20.0f,
                "%.1f"
            );
            ImGui::TableNextColumn();
            ImGui::Text("Sun intensity");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SliderFloat(
                "##misc_ambient_intensity",
                &renderer.m_uniform_data.ambient_intensity,
                0.01f,
                1.0f,
                "%.2f"
            );
            ImGui::TableNextColumn();
            ImGui::Text("Ambient intensity");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SliderFloat("##sun_yaw", &sun_yaw, 0.0f, 360.0f);
            ImGui::TableNextColumn();
            ImGui::Text("Sun yaw");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SliderFloat("##sun_pitch", &sun_pitch, 0.0f, 89.9f);
            ImGui::TableNextColumn();
            ImGui::Text("Sun pitch");

            renderer.set_sun_direction(math::radians(sun_yaw), math::radians(sun_pitch));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            imgui_checkbox("Draw cull AABB", renderer.m_cull_pass_data.draw_cull_aabb);

            // TODO: shadow stuff.

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            imgui_checkbox("SSAO", renderer.m_uniform_data.enable_ssao);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            imgui_checkbox("Fog", renderer.m_uniform_data.enable_fog);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            int render_mode = static_cast<int>(s_render_mode);
            const char* render_modes[] = {"Visibility", "Grad error", "AO"};
            ImGui::ListBox("Render mode", &render_mode, render_modes, ARRAY_SIZE(render_modes));
            s_render_mode = static_cast<RenderMode>(render_mode);
            static_assert(ARRAY_SIZE(render_modes) == RENDER_MODE_COUNT);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SliderFloat(
                "Grad err max",
                &renderer.m_uniform_data.grad_error_max,
                0.01f,
                0.1f,
                "%.2f"
            );

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SeparatorText("TAA");
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            imgui_checkbox("Enable", renderer.m_taa_pass_data.enable);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            int taa_jitter_max_idx = int(renderer.m_taa_jitter_max_idx);
            ImGui::SliderInt("##taa_jitter_max_idx", &taa_jitter_max_idx, 1, 16);
            ImGui::TableNextColumn();
            ImGui::Text("Jitter max idx");
            renderer.m_taa_jitter_max_idx = u32(taa_jitter_max_idx);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SliderFloat(
                "##taa_blend_weight",
                &renderer.m_taa_pass_data.blend_weight,
                0.01f,
                1.0f,
                "%.2f"
            );
            ImGui::TableNextColumn();
            ImGui::Text("Blend weight");

            ImGui::EndTable();
        }

        ImGui::End();

        if (s_need_update_view_matrix) {
            s_need_update_view_matrix = false;
            renderer.update_camera(s_camera.m_position, s_camera.get_view_matrix());
        }

        const bool result = renderer.render(f32(delta_time));
        ASSERT(result);
    }

    return 0;
}
