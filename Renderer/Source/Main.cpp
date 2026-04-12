#include "Common.hpp"
#include "TimeMeter.hpp"
#include "Utils.hpp"
#include "Renderer/Renderer.hpp"
#include "Camera.hpp"
#include "Math/Vec3.hpp"

#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>

#include <stdio.h>

static u8 sKeys[SDL_SCANCODE_COUNT + 1];
static Camera sCamera;
bool sWindowShouldClose = false;
static bool sMouseRelativeMode = true;
static bool sNeedUpdateViewMatrix = true;
static bool sFullscreen = true;
static bool sCullCameraFrozen = false;

static bool SaveCamera(const Camera& camera, const char* path = "Camera.bin")
{
    DEBUG_ASSERT(path);

    FILE* const fp = fopen(path, "wb");
    if (!fp)
    {
        fprintf(stderr, "%s: fopen %s failed: %s\n", __func__, path, strerror(errno));
        return false;
    }
    DEFER(fclose(fp));

    // TODO: very hacky but should be ok for POD.
    if (fwrite(&camera, sizeof(camera), 1, fp) != 1)
    {
        fprintf(stderr, "%s: fwrite %s failed: %s\n", __func__, path, strerror(errno));
        return false;
    }

    return true;
}

static bool LoadCamera(Camera& camera, SDL_Window* window, const char* path = "Camera.bin")
{
    DEBUG_ASSERT(window);
    DEBUG_ASSERT(path);

    FILE* const fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "%s: fopen %s failed: %s\n", __func__, path, strerror(errno));
        return false;
    }
    DEFER(fclose(fp));

    Camera newCamera{};
    // TODO: very hacky but should be ok for POD.
    if (fread(&newCamera, sizeof(newCamera), 1, fp) != 1)
    {
        if (feof(fp))
        {
            fprintf(stderr, "%s: fread %s failed: EOF\n", __func__, path);
        }
        else if (ferror(fp))
        {
            fprintf(stderr, "%s: fread %s failed: %s\n", __func__, path, strerror(errno));
        }
        return false;
    }

    memcpy(&camera, &newCamera, sizeof(camera));

    SDL_SetWindowRelativeMouseMode(window, true);
    sMouseRelativeMode = true;
    sCamera.mLockDirection = false;

    return true;
}

static void ProcessMouse(SDL_Window* window)
{
    assert(window);

    f32 x{};
    f32 y{};
    (void)SDL_GetRelativeMouseState(&x, &y);

    static bool sFirst = true;

    if (sFirst)
    {
        sFirst = false;
        return;
    }

    sCamera.ChangeDirection(x, -y);

    if (sMouseRelativeMode)
    {
        SDL_WarpMouseInWindow(window, 0.0f, 0.0f);
    }

    sNeedUpdateViewMatrix = true;
}

static void ProcessEvent(SDL_Window* window, const SDL_Event& event, Renderer& renderer)
{
    DEBUG_ASSERT(window);

    ImGui_ImplSDL3_ProcessEvent(&event);

    switch (event.type)
    {
    case SDL_EVENT_QUIT:
        sWindowShouldClose = true;
        break;
    case SDL_EVENT_KEY_DOWN:
        sKeys[event.key.scancode] = 1;
        break;
    case SDL_EVENT_KEY_UP:
        sKeys[event.key.scancode] = 0;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        ProcessMouse(window);
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        renderer.PauseRendering(true);
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        renderer.PauseRendering(false);
        break;
    }
}

static void ProcessInput(SDL_Window* window, f32 deltaTime, Renderer& renderer)
{
    DEBUG_ASSERT(window);

    if (sKeys[SDL_SCANCODE_W])
    {
        sCamera.Move(Camera::MoveDirection::Forward, deltaTime);
        sNeedUpdateViewMatrix = true;
    }
    if (sKeys[SDL_SCANCODE_S])
    {
        sCamera.Move(Camera::MoveDirection::Backward, deltaTime);
        sNeedUpdateViewMatrix = true;
    }
    if (sKeys[SDL_SCANCODE_D])
    {
        sCamera.Move(Camera::MoveDirection::Right, deltaTime);
        sNeedUpdateViewMatrix = true;
    }
    if (sKeys[SDL_SCANCODE_A])
    {
        sCamera.Move(Camera::MoveDirection::Left, deltaTime);
        sNeedUpdateViewMatrix = true;
    }
    if (sKeys[SDL_SCANCODE_Z])
    {
        sCamera.Move(Camera::MoveDirection::Down, deltaTime);
        sNeedUpdateViewMatrix = true;
    }
    if (sKeys[SDL_SCANCODE_X])
    {
        sCamera.Move(Camera::MoveDirection::Up, deltaTime);
        sNeedUpdateViewMatrix = true;
    }

    if (sKeys[SDL_SCANCODE_PERIOD])
    {
        sKeys[SDL_SCANCODE_PERIOD] = 0;
        sFullscreen ^= true;
        SDL_SetWindowFullscreen(window, sFullscreen);
    }

    if (sKeys[SDL_SCANCODE_ESCAPE])
    {
        sKeys[SDL_SCANCODE_ESCAPE] = 0;
        sWindowShouldClose = true;
    }

    if (sKeys[SDL_SCANCODE_M])
    {
        sKeys[SDL_SCANCODE_M] = 0;
        if (sMouseRelativeMode)
        {
            SDL_SetWindowRelativeMouseMode(window, false);
            sMouseRelativeMode = false;
            sCamera.mLockDirection = true;
        }
        else
        {
            SDL_SetWindowRelativeMouseMode(window, true);
            sMouseRelativeMode = true;
            sCamera.mLockDirection = false;
        }
    }

    if (sKeys[SDL_SCANCODE_U])
    {
        sKeys[SDL_SCANCODE_U] = 0;
        renderer.mEnableUI ^= true;
    }

    if (sKeys[SDL_SCANCODE_C])
    {
        sKeys[SDL_SCANCODE_C] = 0;
        sCullCameraFrozen ^= true;
        renderer.FreezeCullCamera(sCullCameraFrozen);
    }
}

static void ImguiCheckbox(const char* label, u32& value)
{
    bool boolValue = value;
    ImGui::Checkbox(label, &boolValue);
    value = boolValue;
}

int main()
{
    Renderer renderer{};

    if (!renderer.Init())
    {
        fprintf(stderr, "renderer initialization failed\n");
        return 1;
    }
    DEFER(renderer.Cleanup());

    bool enableCameraLoading = true;

    renderer.mEnableUI = true;
    renderer.mUniformData.taaEnable = 1;
    renderer.ChangeRenderMode(Renderer::RenderMode::Normal);

    sCamera.mPosition = {9.4f, 7.4f, 0.8f};
    sCamera.mYaw = Radians(-85.0f);
    sCamera.mPitch = Radians(0.0f);
    sCamera.mWorldUp = WORLD_Y;
    sCamera.mSpeed = 10.0f;
    sCamera.mMouseSensitivity = 0.002f;
    sCamera.mPitchClamp = Radians(89.0f);
    if (enableCameraLoading)
    {
        if (!LoadCamera(sCamera, renderer.mWindow))
        {
            fprintf(stderr, "camera loading failed\n");
        }
    }
    sCamera.UpdateVectors();
    renderer.UpdateCamera(sCamera.mPosition, sCamera.GetViewMatrix());

    u64 performanceCounter = SDL_GetPerformanceCounter();
    const f64 performancePeriod = 1.0 / f64(SDL_GetPerformanceFrequency());
    u64 lastPerformanceCounter = performanceCounter;

    // To prevent a very big first measurement since mStartTime == 0
    // and it uses MeasureBetween function.
    gTimeMeters[TimeMeter::Frame].Start();

    while (!sWindowShouldClose)
    {
        lastPerformanceCounter = performanceCounter;
        performanceCounter = SDL_GetPerformanceCounter();
        const f64 deltaTime = f64(performanceCounter - lastPerformanceCounter) * performancePeriod;

        gTimeMeters[TimeMeter::Frame].MeasureBetween();

        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            ProcessEvent(renderer.mWindow, event, renderer);
        }
        ProcessInput(renderer.mWindow, f32(deltaTime), renderer);

        (void)renderer.StartNewFrame();

        ImGui::Begin("Main");

        if (ImGui::BeginTable("Info", 2))
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SeparatorText("Camera");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Position");
            ImGui::TableNextColumn();
            ImGui::Text(
                "%.1f %.1f %.1f",
                sCamera.mPosition.X(),
                sCamera.mPosition.Y(),
                sCamera.mPosition.Z()
            );

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Direction");
            ImGui::TableNextColumn();
            ImGui::Text(
                "%.1f %.1f %.1f",
                sCamera.mDirection.X(),
                sCamera.mDirection.Y(),
                sCamera.mDirection.Z()
            );

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Button("Save"))
            {
                if (!SaveCamera(sCamera))
                {
                    fprintf(stderr, "camera saving failed\n");
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Load"))
            {
                if (!LoadCamera(sCamera, renderer.mWindow))
                {
                    fprintf(stderr, "camera loading failed\n");
                }
                sCamera.UpdateVectors();
                renderer.UpdateCamera(sCamera.mPosition, sCamera.GetViewMatrix());
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SeparatorText("Misc");
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SliderFloat(
                "##MiscSunIntensity",
                &renderer.mUniformData.sunIntensity,
                1.0f,
                20.0f,
                "%.1f"
            );
            ImGui::TableNextColumn();
            ImGui::Text("Sun intensity");
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SliderFloat(
                "##MiscAmbientIntensity",
                &renderer.mUniformData.ambientIntensity,
                0.01f,
                1.0f,
                "%.2f"
            );
            ImGui::TableNextColumn();
            ImGui::Text("Ambient intensity");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImguiCheckbox("Draw cull AABB", renderer.mUniformData.drawCullAABB);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool drawGradError = renderer.mRenderMode == Renderer::RenderMode::GradError;
            // TODO: enum when > 2 render modes, not bool.
            if (ImGui::Checkbox("Grad error", &drawGradError))
            {
                if (drawGradError)
                {
                    renderer.ChangeRenderMode(Renderer::RenderMode::GradError);
                }
                else
                {
                    renderer.ChangeRenderMode(Renderer::RenderMode::Normal);
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SliderFloat(
                "Grad err max",
                &renderer.mUniformData.gradErrorMax,
                0.01f,
                0.1f,
                "%.2f"
            );

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SeparatorText("TAA");
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImguiCheckbox("Enable", renderer.mUniformData.taaEnable);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            int taaJitterMaxIdx = int(renderer.mTaaJitterMaxIdx);
            ImGui::SliderInt("##TaaJitterMaxIdx", &taaJitterMaxIdx, 1, 16);
            ImGui::TableNextColumn();
            ImGui::Text("Jitter max idx");
            renderer.mTaaJitterMaxIdx = u32(taaJitterMaxIdx);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SliderFloat(
                "##TaaBlendWeight",
                &renderer.mUniformData.taaBlendWeight,
                0.01f,
                1.0f,
                "%.2f"
            );
            ImGui::TableNextColumn();
            ImGui::Text("Blend weight");

            ImGui::EndTable();
        }

        ImGui::End();

        if (sNeedUpdateViewMatrix)
        {
            sNeedUpdateViewMatrix = false;
            renderer.UpdateCamera(sCamera.mPosition, sCamera.GetViewMatrix());
        }

        const bool result = renderer.Render(f32(deltaTime));
        ASSERT(result);
    }

    return 0;
}
