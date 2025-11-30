#include "Common.hpp"
#include "Arena.hpp"
#include "Camera.hpp"
#include "Physics/World.hpp"
#include "TimeMeter.hpp"
#include "Math/Quat.hpp"
#include "Math/Utils.hpp"
#include "Utils.hpp"

#include "Renderer/Renderer.hpp"

#include "SDL3/SDL.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

#include <assert.h>
#include <stdio.h>

struct Bodies
{
    Body::Id mCollider;
    Body::Id mFloor;

    static constexpr int WALL_ROWS = 4;
    static constexpr int WALL_COLUMNS = 4;
    Body::Id mWall[WALL_ROWS * WALL_COLUMNS];

    static constexpr int SPHERES_DIMENSIONS_X = 5;
    static constexpr int SPHERES_DIMENSIONS_Y = 5;
    static constexpr int SPHERES_DIMENSIONS_Z = 5;
    Body::Id mSpheres[SPHERES_DIMENSIONS_X * SPHERES_DIMENSIONS_Y * SPHERES_DIMENSIONS_Z];

    struct Table
    {
        int mCount;
        int mChosen;
        Body::Id mIds[PHYSICS_MAX_BODIES];
        const char* mStrings[PHYSICS_MAX_BODIES];

        void Add(Body::Id id, const char* string)
        {
            assert(mCount < PHYSICS_MAX_BODIES);
            if (mCount >= PHYSICS_MAX_BODIES)
            {
                return;
            }
            mIds[mCount] = id;
            mStrings[mCount] = string;
            ++mCount;
        }
    };

    Table mTable;
};

static Camera sCamera;
static bool sNeedUpdateViewMatrix = true;
static bool sFullscreen = true;
static bool sEnableUI = true;
static bool sMouseRelativeMode = true;
static bool sPhysicsStepped;
static World sWorld;
static Bodies sBodies;
constexpr f32 TIME_STEP = 1.0f / 60.0f;

static u8 sKeys[SDL_SCANCODE_COUNT + 1];

static void ResetWorld(World& world, Bodies& bodies, f32 timeStep, bool accurateSlowMotion)
{
    world.Reset();
    bodies = {};
    bodies.mTable.Add(-1, "None");

    // Let physics run with a small time step size, but render as if it's normal,
    // therefore we get accurate simulation (small step size) and slow motion.
    if (accurateSlowMotion)
    {
        timeStep /= 10.0f;
    }

#ifdef PHYSICS_COLLIDE_ONLY
    world.Init({0.0f, 0.0f, 0.0f}, timeStep, 10);
#else
    world.Init({0.0f, -9.81f, 0.0f}, timeStep, 10);
#endif

    ConvexHull colliderHull{};
    colliderHull.InitTetrahedron(Vec3{2.0f});

    ConvexHull floorHull{};
    constexpr Vec3 FLOOR_SIZE = {200.0f, 5.0f, 200.0f};
    floorHull.InitBox(FLOOR_SIZE);

    constexpr f32 WALL_BOX_WIDTH = 2.0f;
    ConvexHull wallHull{};
    wallHull.InitBox(Vec3{WALL_BOX_WIDTH});

    const ConvexHull::Id colliderHullId = world.AddConvexHull(colliderHull);
    const ConvexHull::Id floorHullId = world.AddConvexHull(floorHull);
    const ConvexHull::Id wallHullId = world.AddConvexHull(wallHull);

    Body bodyDef{};

    world.BodyInitConvexHull(bodyDef, FLT_MAX, floorHullId);
    bodyDef.mPosition.Y() = -FLOOR_SIZE.Y() * 0.5f;
    bodyDef.mFriction = 0.6f;
    bodies.mFloor = world.SetFloor(bodyDef);
    assert(world.IsBodyIdValid(bodies.mFloor));

    world.BodyInitConvexHull(bodyDef, 20000.0f, colliderHullId);
    bodyDef.mPosition = Vec3{38.0f, 2.0f, -30.0f};
    bodyDef.mOrientation
        = Quat::FromAxis(Radians(40.0f), WORLD_Y) * Quat::FromAxis(Radians(83.0f), WORLD_Z);
    bodyDef.mVelocity = Rotate(bodyDef.mOrientation, WORLD_Y) * 52.9f;
    bodyDef.mAngularVelocity = {10.0f, 0.0f, 0.0f};
    bodies.mCollider = world.AddBody(bodyDef);
    assert(world.IsBodyIdValid(bodies.mCollider));
    bodies.mTable.Add(bodies.mCollider, "Collider");

    constexpr f32 BODIES_GAP_ROWS = 0.01f;
    constexpr f32 BODIES_GAP_COLUMNS = 0.05f;

    world.BodyInitConvexHull(bodyDef, 1500.0f, wallHullId);

    for (int i = 0; i < bodies.WALL_COLUMNS; ++i)
    {
        for (int j = 0; j < bodies.WALL_ROWS; ++j)
        {
            const int index = i * bodies.WALL_ROWS + j;
            bodyDef.mPosition
                = {(WALL_BOX_WIDTH + BODIES_GAP_COLUMNS) * static_cast<f32>(i),
                   WALL_BOX_WIDTH / 2.0f + (WALL_BOX_WIDTH + BODIES_GAP_ROWS) * static_cast<f32>(j),
                   0.0f};
            bodies.mWall[index] = world.AddBody(bodyDef);
            assert(world.IsBodyIdValid(bodies.mWall[index]));
        }
    }

    constexpr f32 SPHERE_RADIUS = 0.2f;
    constexpr f32 SPHERE_DIAMETER = SPHERE_RADIUS * 2.0f;
    constexpr f32 SPHERES_GAP = 0.001f;
    world.BodyInitSphere(bodyDef, 1000.0f, SPHERE_RADIUS);
    for (int x = 0; x < bodies.SPHERES_DIMENSIONS_X; ++x)
    {
        for (int y = 0; y < bodies.SPHERES_DIMENSIONS_Y; ++y)
        {
            for (int z = 0; z < bodies.SPHERES_DIMENSIONS_Z; ++z)
            {
                const int index
                    = (x * bodies.SPHERES_DIMENSIONS_Y + y) * bodies.SPHERES_DIMENSIONS_Z + z;
                // clang-format off
                bodyDef.mPosition = {
                    0.0f  + (SPHERE_DIAMETER + SPHERES_GAP) * static_cast<f32>(x),
                    SPHERE_RADIUS + (SPHERE_DIAMETER + SPHERES_GAP) * static_cast<f32>(y),
                    -20.0f  + (SPHERE_DIAMETER + SPHERES_GAP) * static_cast<f32>(z)
                };
                // clang-format on
                bodies.mSpheres[index] = world.AddBody(bodyDef);
                assert(world.IsBodyIdValid(bodies.mSpheres[index]));
            }
        }
    }
}

static void DrawBodies(const Bodies& bodies)
{
    gRenderer.DrawBox(
        sWorld.GetPosition(bodies.mFloor),
        sWorld.GetOrientation(bodies.mFloor),
        sWorld.GetScale(bodies.mFloor),
        {100, 100, 100}
    );
    gRenderer.DrawTetrahedron(
        sWorld.GetPosition(bodies.mCollider),
        sWorld.GetOrientation(bodies.mCollider),
        sWorld.GetScale(bodies.mCollider),
        {200, 80, 80}
    );
    for (size_t i = 0; i < ARRAY_SIZE(bodies.mWall); ++i)
    {
        gRenderer.DrawBox(
            sWorld.GetPosition(bodies.mWall[i]),
            sWorld.GetOrientation(bodies.mWall[i]),
            sWorld.GetScale(bodies.mWall[i]),
            {150, 150, 150}
        );
    }
    for (size_t i = 0; i < ARRAY_SIZE(bodies.mSpheres); ++i)
    {
        gRenderer.DrawSphere(
            sWorld.GetPosition(bodies.mSpheres[i]),
            sWorld.GetOrientation(bodies.mSpheres[i]),
            sWorld.GetRadius(bodies.mSpheres[i]),
            {240, 140, 140}
        );
    }
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

static void ProcessEvent(SDL_Window* window, const SDL_Event& event, bool& windowShouldClose)
{
    assert(window);

    ImGui_ImplSDL3_ProcessEvent(&event);

    switch (event.type)
    {
    case SDL_EVENT_QUIT:
        windowShouldClose = true;
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
        gRenderer.PauseRendering(true);
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        gRenderer.PauseRendering(false);
        break;
    }
}

static void ProcessInput(SDL_Window* window, f32 deltaTime, bool& windowShouldClose)
{
    assert(window);

    if (sKeys[SDL_SCANCODE_LSHIFT])
    {
        sCamera.mSpeed = 100.0f;
    }
    else
    {
        sCamera.mSpeed = 10.0f;
    }
    const int chosenBody = sBodies.mTable.mChosen;
    const Body::Id chosenId = sBodies.mTable.mIds[chosenBody];
    if (chosenId != -1)
    {
        constexpr f32 BODY_SPEED = 5.0f;
        Vec3 position = sWorld.GetPosition(sBodies.mTable.mIds[chosenBody]);
        const f32 deltaPosition = BODY_SPEED * deltaTime;
        if (sKeys[SDL_SCANCODE_W])
        {
            position.Z() -= deltaPosition;
        }
        if (sKeys[SDL_SCANCODE_S])
        {
            position.Z() += deltaPosition;
        }
        if (sKeys[SDL_SCANCODE_D])
        {
            position.X() += deltaPosition;
        }
        if (sKeys[SDL_SCANCODE_A])
        {
            position.X() -= deltaPosition;
        }
        if (sKeys[SDL_SCANCODE_Z])
        {
            position.Y() -= deltaPosition;
        }
        if (sKeys[SDL_SCANCODE_X])
        {
            position.Y() += deltaPosition;
        }
        sWorld.SetPosition(sBodies.mTable.mIds[chosenBody], position);
    }
    else
    {
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
        windowShouldClose = true;
    }

    if (sKeys[SDL_SCANCODE_U])
    {
        sKeys[SDL_SCANCODE_U] = 0;
        sEnableUI ^= true;
        gRenderer.EnableUI(sEnableUI);
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

    if (sKeys[SDL_SCANCODE_R])
    {
        sKeys[SDL_SCANCODE_R] = 0;
        ResetWorld(sWorld, sBodies, TIME_STEP, sKeys[SDL_SCANCODE_LSHIFT]);
    }

    if (sKeys[SDL_SCANCODE_P])
    {
        sKeys[SDL_SCANCODE_P] = 0;
        sPhysicsStepped = true;
    }
}

static void ImGuiTableRowStringFloat(const char* name, f64 value)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s", name);
    ImGui::TableNextColumn();
    ImGui::Text("%.1f\n", value);
}

int main()
{
    // NOTE: losing address sanitizer...
    constexpr int MEMORY_SIZE = 4'000'000;
    void* memory = Utils::xmalloc(MEMORY_SIZE);
    // DEFER(SAFE_FREE(memory));

    Utils::MemoryDivider memoryDivider{};
    memoryDivider.Init(memory, MEMORY_SIZE);

    MemorySlice memorySlice{};

    memorySlice = memoryDivider.Take(64'000);
    assert(memorySlice.mData);
    gArenaStatic.Init(memorySlice.mData, memorySlice.mCount, "Static");

    memorySlice = memoryDivider.Take(1024'000);
    assert(memorySlice.mData);
    gArenaFrame.Init(memorySlice.mData, memorySlice.mCount, "Frame");

    memorySlice = memoryDivider.Take(16'000);
    assert(memorySlice.mData);
    gArenaSwapchain.Init(memorySlice.mData, memorySlice.mCount, "Swapchain");

    memorySlice = memoryDivider.TakeRest();
    assert(memorySlice.mData);
    gArenaReset.Init(memorySlice.mData, memorySlice.mCount, "Reset");

    assert(gArenaStatic.mBuffer + gArenaStatic.mBufferSize < gArenaFrame.mBuffer);
    assert(gArenaFrame.mBuffer + gArenaFrame.mBufferSize < gArenaSwapchain.mBuffer);
    assert(gArenaSwapchain.mBuffer + gArenaSwapchain.mBufferSize < gArenaReset.mBuffer);
    assert(gArenaReset.mBuffer + gArenaReset.mBufferSize < static_cast<u8*>(memory) + MEMORY_SIZE);

    if (!SDL_SetHint("SDL_VIDEO_DRIVER", "x11"))
    {
        fprintf(stderr, "SDL_SetHint(\"SDL_VIDEO_DRIVER\", \"x11\") failed: %s\n", SDL_GetError());
        fprintf(
            stderr,
            "WARNING: I've had some strange bugs on Wayland, including hangs, segfaults on "
            "cleanup (glfw), also RenderDoc doesn't work on Wayland. "
            "It seems that using X11 (or Xwayland) is better for now.\n"
        );
    }

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return 1;
    }
    // DEFER(SDL_Quit());

    SDL_Window* const window = SDL_CreateWindow(
        "vulkan",
        800,
        600,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MOUSE_GRABBED | SDL_WINDOW_FULLSCREEN
            | SDL_WINDOW_MOUSE_RELATIVE_MODE
    );
    if (!window)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    // DEFER(SDL_DestroyWindow(window));

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowRelativeMouseMode(window, true);

    if (!gRenderer.Init(window))
    {
        return 1;
    }
    DEFER(gRenderer.Cleanup());

    sCamera.mPosition = {9.0f, 5.3f, 13.1f};
    sCamera.mYaw = Radians(-28.0f);
    sCamera.mPitch = Radians(-10.0f);
    sCamera.mWorldUp = WORLD_Y;
    sCamera.mSpeed = 10.0f;
    sCamera.mMouseSensitivity = 0.002f;
    sCamera.mPitchClamp = Radians(89.0f);
    sCamera.UpdateVectors();
    gRenderer.UpdateCamera(sCamera.mPosition, sCamera.GetViewMatrix());

    bool windowShouldClose = false;

    ResetWorld(sWorld, sBodies, TIME_STEP, false);

    u64 performanceCounter = SDL_GetPerformanceCounter();
    const f64 performancePeriod = 1.0 / static_cast<f64>(SDL_GetPerformanceFrequency());
    u64 lastPerformanceCounter = performanceCounter;
    u64 frameCount = 0;
    f64 timeAccumulator = 0.0f;

    bool enableShadowCascadeColors = false;
    bool enableShadowPcf = true;
    bool enableShadowCascadesUpdate = true;
    bool enableShadowTexelColoring = false;
    bool enablePhysicsStepping = false;
    bool physicsDrawSpheres = false;
    bool physicsDrawContacts = false;
    int viewChosen = 0;
    gRenderer.EnableShadowCascadesColor(enableShadowCascadeColors);
    gRenderer.EnableShadowPcf(enableShadowPcf);
    gRenderer.ChooseView(static_cast<u32>(viewChosen));
    gRenderer.EnableShadowCascadesUpdate(enableShadowCascadesUpdate);
    gRenderer.EnableShadowTexelColoring(enableShadowTexelColoring);
    gRenderer.EnableUI(sEnableUI);

    f32 sunYaw = 45.0f;
    f32 sunPitch = 45.0f;
    Vec3 sunColor = Vec3{1.0f};

    Utils::FpsCounter fpsCounter = {static_cast<f64>(performanceCounter) * performancePeriod, 0};
    f64 fps = 0.0;

    gRenderer.SetLightDirection(Radians(sunYaw), Radians(sunPitch));
    gRenderer.SetLightColor(Vec3{1.0f});

    // To prevent a very big first measurement since mStartTime == 0
    // and it uses MeasureBetween function.
    gTimeMeters[TimeMeter::Frame].Start();

    bool result = false;
    (void)result;

    while (!windowShouldClose)
    {
        lastPerformanceCounter = performanceCounter;
        performanceCounter = SDL_GetPerformanceCounter();

        gArenaFrame.FreeAll();

        const f64 deltaTime
            = static_cast<f64>(performanceCounter - lastPerformanceCounter) * performancePeriod;

        gTimeMeters[TimeMeter::ProcessEvents].Start();
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            ProcessEvent(window, event, windowShouldClose);
        }
        gTimeMeters[TimeMeter::ProcessEvents].End();

        gTimeMeters[TimeMeter::ProcessInput].Start();
        ProcessInput(window, static_cast<f32>(deltaTime), windowShouldClose);
        gTimeMeters[TimeMeter::ProcessInput].End();

        if (sNeedUpdateViewMatrix)
        {
            sNeedUpdateViewMatrix = false;
            gRenderer.UpdateCamera(sCamera.mPosition, sCamera.GetViewMatrix());
        }

        result = gRenderer.StartNewFrame();
        assert(result);

        gTimeMeters[TimeMeter::Physics].Start();
        if (enablePhysicsStepping)
        {
            if (sPhysicsStepped)
            {
                sPhysicsStepped = false;
                sWorld.Step();
            }
        }
        else
        {
            // TODO: causes temporal aliasing, should probably lerp the state
            // after integrating.
            while (timeAccumulator >= TIME_STEP)
            {
                sWorld.Step();
                timeAccumulator -= TIME_STEP;
            }

            timeAccumulator += deltaTime;
        }
        gTimeMeters[TimeMeter::Physics].End();

#ifdef PHYSICS_DEBUG
        sWorld.DebugDraw(physicsDrawSpheres, physicsDrawContacts);
        // sWorld.DebugPrintBodiesInfo();
#endif

        gTimeMeters[TimeMeter::UiDraw].Start();

        ImGui::Begin("Info");

        if (ImGui::BeginTable("Info", 2))
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SeparatorText("System");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("GPU");
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", gRenderer.GetGpuName());

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
            ImGui::SeparatorText("Time (us)");

            ImGuiTableRowStringFloat(
                "Process events",
                gTimeMeters[TimeMeter::ProcessEvents].GetUs()
            );

            ImGuiTableRowStringFloat("Process input", gTimeMeters[TimeMeter::ProcessInput].GetUs());
            ImGuiTableRowStringFloat("Text draw", gTimeMeters[TimeMeter::UiDraw].GetUs());
            ImGuiTableRowStringFloat(
                "Create HGrid",
                gTimeMeters[TimeMeter::PhysicsCreateHGrid].GetUs()
            );
            ImGuiTableRowStringFloat(
                "Manifolds",
                gTimeMeters[TimeMeter::PhysicsContactManifold].GetUs()
            );
            ImGuiTableRowStringFloat(
                "Inertias world",
                gTimeMeters[TimeMeter::PhysicsInertiasWorld].GetUs()
            );
            ImGuiTableRowStringFloat(
                "Integrate forces",
                gTimeMeters[TimeMeter::PhysicsIntegrateForces].GetUs()
            );
            ImGuiTableRowStringFloat("Prestep", gTimeMeters[TimeMeter::PhysicsPrestep].GetUs());
            ImGuiTableRowStringFloat(
                "Apply impulses",
                gTimeMeters[TimeMeter::PhysicsApplyImpulse].GetUs()
            );
            ImGuiTableRowStringFloat(
                "Integrate velocities",
                gTimeMeters[TimeMeter::PhysicsIntegrateVelocities].GetUs()
            );
            ImGuiTableRowStringFloat("Physics", gTimeMeters[TimeMeter::Physics].GetUs());
            ImGuiTableRowStringFloat(
                "New frame fence",
                gTimeMeters[TimeMeter::NewFrameFence].GetUs()
            );
            ImGuiTableRowStringFloat(
                "Shadow cascades",
                gTimeMeters[TimeMeter::UpdateShadowCascades].GetUs()
            );
            ImGuiTableRowStringFloat("Frame", gTimeMeters[TimeMeter::Frame].GetUs());
            ImGuiTableRowStringFloat("FPS", fps);

            ImGui::EndTable();
        }
        ImGui::SeparatorText("Memory");
        if (ImGui::BeginTable("Arenas", 3))
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Arena");
            ImGui::TableNextColumn();
            ImGui::Text("Full/Max, %%");
            for (size_t i = 0; i < ARRAY_SIZE(gArenas); ++i)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", gArenas[i]->mName);
                ImGui::TableNextColumn();
                ImGui::Text(
                    "%.2f/%.2f",
                    static_cast<f64>(gArenas[i]->mCurrentOffset)
                        / static_cast<f64>(gArenas[i]->mBufferSize) * 100.0,
                    static_cast<f64>(gArenas[i]->mMaxOffset)
                        / static_cast<f64>(gArenas[i]->mBufferSize) * 100.0

                );
            }

            ImGui::EndTable();
        }

        ImGui::SeparatorText("Broad-phase");
        if (ImGui::BeginTable("Broad-phase", 2))
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Manifolds load factor");

            ImGui::TableNextColumn();
            ImGui::Text(
                "%.2f\n",
                static_cast<f64>(sWorld.GetContactManifoldsCount()) / PHYSICS_MAX_CONTACT_MANIFOLDS
            );

            ImGui::EndTable();
        }

#ifndef PHYSICS_NO_BROADPHASE
        ImGui::SeparatorText("Broad-phase");
        if (ImGui::BeginTable("Broad-phase", 2))
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("At levels");
            ImGui::TableNextColumn();
            static_assert(ARRAY_SIZE(HGrid::LEVEL_SIZES) == 2);
            ImGui::Text(
                "%d|%d",
                sWorld.GetHGrid().mObjectsAtLevel[0],
                sWorld.GetHGrid().mObjectsAtLevel[1]
            );

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Ratio");
            ImGui::TableNextColumn();
            const int bodiesCount = sWorld.GetBodiesCount();
            const int requiredComparisonsNoBroadPhase
                = bodiesCount * bodiesCount / 2 - bodiesCount / 2;
            const f64 ratio = sWorld.GetHGrid().mTestsCount == 0
                ? 0.0
                : static_cast<f64>(requiredComparisonsNoBroadPhase) / sWorld.GetHGrid().mTestsCount;
            ImGui::Text("%.1f", ratio);

            ImGui::EndTable();
        }
#endif

        ImGui::End();

        ImGui::Begin("Settings");
        ImGui::SeparatorText("Cascaded shadow maps");
        bool changed = ImGui::Checkbox("Color cascades", &enableShadowCascadeColors);
        if (changed)
        {
            gRenderer.EnableShadowCascadesColor(enableShadowCascadeColors);
            changed = false;
        }
        changed = ImGui::Checkbox("PCF", &enableShadowPcf);
        if (changed)
        {
            gRenderer.EnableShadowPcf(enableShadowPcf);
            changed = false;
        }
        changed = ImGui::Checkbox("Cascade update", &enableShadowCascadesUpdate);
        if (changed)
        {
            gRenderer.EnableShadowCascadesUpdate(enableShadowCascadesUpdate);
            changed = false;
        }
        changed = ImGui::Checkbox("Texel coloring", &enableShadowTexelColoring);
        if (changed)
        {
            gRenderer.EnableShadowTexelColoring(enableShadowTexelColoring);
            changed = false;
        }
        changed = ImGui::SliderInt("View", &viewChosen, 0, RENDERER_SHADOW_MAP_CASCADE_COUNT);
        if (changed)
        {
            gRenderer.ChooseView(static_cast<u32>(viewChosen));
            changed = false;
        }

        ImGui::SeparatorText("Physics");
        ImGui::Checkbox("Step", &enablePhysicsStepping);
        ImGui::Checkbox("Draw spheres", &physicsDrawSpheres);
        ImGui::Checkbox("Draw contacts", &physicsDrawContacts);
        ImGui::ListBox(
            "Bodies",
            &sBodies.mTable.mChosen,
            sBodies.mTable.mStrings,
            sBodies.mTable.mCount
        );

        ImGui::SeparatorText("Sun");
        changed |= ImGui::SliderFloat("Yaw", &sunYaw, 0.0f, 360.0f);
        changed |= ImGui::SliderFloat("Pitch", &sunPitch, 0.0f, 180.0f);
        if (changed)
        {
            gRenderer.SetLightDirection(Radians(sunYaw), Radians(sunPitch));
            changed = false;
        }
        changed |= ImGui::SliderFloat("R", &sunColor.R(), 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("G", &sunColor.G(), 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("B", &sunColor.B(), 0.0f, 1.0f);
        if (changed)
        {
            gRenderer.SetLightColor(sunColor);
            changed = false;
        }
        ImGui::End();

        gTimeMeters[TimeMeter::UiDraw].End();

        DrawBodies(sBodies);

        result = gRenderer.Render();
        assert(result);

        fpsCounter.Update(fps, static_cast<f64>(performanceCounter) * performancePeriod);

        ++frameCount;
        gTimeMeters[TimeMeter::Frame].MeasureBetween();

        if ((frameCount & (frameCount - 1)) == 0)
        {
            printf(
                "Frame = %lu, delta time = %.1f us, average time = %.1f us\n",
                frameCount,
                deltaTime * 1'000'000.0,
                gTimeMeters[TimeMeter::Frame].GetUs()
            );
        }

        // if (deltaTime > 0.1f)
        // {
        //     printf("Skill issue, frame time > 100 ms\n");
        //     return 0;
        // }
    }

    return 0;
}
