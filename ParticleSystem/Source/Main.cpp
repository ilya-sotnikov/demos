#include "Common.hpp"
#include "TimeMeter.hpp"
#include "Utils.hpp"
#include "Renderer.hpp"
#include "Camera.hpp"

#include <SDL3/SDL.h>

#include <stdio.h>

static u8 sKeys[SDL_SCANCODE_COUNT + 1];
static Camera sCamera;
bool sWindowShouldClose = false;
static bool sMouseRelativeMode = true;
static bool sNeedUpdateViewMatrix = true;
static bool sNeedResetParticleSystem = true;
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

static void ProcessInput(SDL_Window* window, f32 deltaTime)
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

    if (sKeys[SDL_SCANCODE_R])
    {
        sKeys[SDL_SCANCODE_R] = 0;
        sNeedResetParticleSystem = true;
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

#if defined(PARTICLE_SYSTEM_DEMO)
    sCamera.mPosition = {-8.0f, 7.0f, 8.0f};
    sCamera.mYaw = Radians(45.0f);
    sCamera.mPitch = Radians(-30.0f);
#elif defined(PARTICLE_SYSTEM_STRESS_TEST)
    sCamera.mPosition = {-2.0f, 12.0f, 15.0f};
    sCamera.mYaw = Radians(90.0f);
    sCamera.mPitch = Radians(-60.0f);
#else
    sCamera.mPosition = {0.0f, 0.0f, 6.0f};
#endif
    sCamera.mWorldUp = WORLD_Y;
    sCamera.mSpeed = 10.0f;
    sCamera.mMouseSensitivity = 0.002f;
    sCamera.mPitchClamp = Radians(89.0f);
    sCamera.UpdateVectors();
    renderer.UpdateCamera(sCamera.GetViewMatrix());

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
        ProcessInput(renderer.mWindow, f32(deltaTime));

        (void)renderer.StartNewFrame();

        if (sNeedUpdateViewMatrix)
        {
            sNeedUpdateViewMatrix = false;
            renderer.UpdateCamera(sCamera.GetViewMatrix());
        }

        if (sNeedResetParticleSystem)
        {
            sNeedResetParticleSystem = false;
            renderer.ResetParticleSystem();
        }

        const bool result = renderer.Render(f32(deltaTime));
        ASSERT(result);
    }

    return 0;
}
