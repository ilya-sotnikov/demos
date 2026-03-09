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

    renderer.mEnableUI = true;
    renderer.mUniformData.exposure = 4.0f;

    sCamera.mPosition = {9.4f, 7.4f, 0.8f};
    sCamera.mYaw = Radians(-85.0f);
    sCamera.mPitch = Radians(0.0f);
    sCamera.mWorldUp = WORLD_Y;
    sCamera.mSpeed = 10.0f;
    sCamera.mMouseSensitivity = 0.002f;
    sCamera.mPitchClamp = Radians(89.0f);
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
            ImGui::SliderFloat("Exposure", &renderer.mUniformData.exposure, 1.0, 128.0, "%.1f");

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
