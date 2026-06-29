#include "Common.hpp"

#include "Camera.hpp"
#include "Math.hpp"
#include "Shaders/SharedDef.hlsli"

#include <stdio.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

static u8 sKeys[SDL_SCANCODE_COUNT + 1];
static Camera sCamera;
static bool sShouldWindowClose;
static bool sMouseRelativeMode = true;
static SDL_GPUGraphicsPipeline* sPipeline;

struct ShaderDesc
{
    SDL_GPUDevice* device;
    SDL_GPUShaderStage stage;
    const char* path;
    u32 samplerCount;
    u32 uniformBufferCount;
    u32 storageBufferCount;
    u32 storageTextureCount;
};

static SDL_GPUShader* LoadShader(const ShaderDesc&& desc)
{
    DEBUG_ASSERT(desc.device);
    DEBUG_ASSERT(desc.path);

    size_t codeSize = 0;
    void* const code = SDL_LoadFile(desc.path, &codeSize);
    if (!code)
    {
        SDL_Log("SDL_LoadFile failed for %s: %s", desc.path, SDL_GetError());
        return nullptr;
    }
    DEFER(SDL_free(code));

    const SDL_GPUShaderCreateInfo shaderInfo = {
        .code_size = codeSize,
        .code = static_cast<u8*>(code),
        .entrypoint = "Main",
        .format = SDL_GPU_SHADERFORMAT_DXIL,
        .stage = desc.stage,
        .num_samplers = desc.samplerCount,
        .num_storage_textures = desc.storageTextureCount,
        .num_storage_buffers = desc.storageBufferCount,
        .num_uniform_buffers = desc.uniformBufferCount,
    };
    SDL_GPUShader* const shader = SDL_CreateGPUShader(desc.device, &shaderInfo);
    if (!shader)
    {
        SDL_Log("SDL_CreateGPUShader failed: %s\n", SDL_GetError());
        return nullptr;
    }

    return shader;
}

static bool RecompilePipelines(SDL_GPUDevice* device, SDL_Window* window)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(window);

    const char* args[] = {"cmake", "--build", ".", "-t", "Shaders", nullptr};

    SDL_Process* const process = SDL_CreateProcess(args, false);
    if (!process)
    {
        SDL_Log("SDL_CreateProcess failed");
        return false;
    }
    DEFER(SDL_DestroyProcess(process));

    int exitCode = 0;
    if (!SDL_WaitProcess(process, true, &exitCode))
    {
        SDL_Log("SDL process failed");
    }
    if (exitCode != 0)
    {
        return false;
    }

    SDL_GPUShader* const vertexShader = LoadShader({
        .device = device,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .path = "Grid.vert.hlsl.ir",
        .uniformBufferCount = 1,
    });
    if (!vertexShader)
    {
        return false;
    }
    DEFER(SDL_ReleaseGPUShader(device, vertexShader));

    SDL_GPUShader* const fragmentShader = LoadShader({
        .device = device,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .path = "Grid.frag.hlsl.ir",
        .uniformBufferCount = 1,
    });
    if (!fragmentShader)
    {
        return false;
    }
    DEFER(SDL_ReleaseGPUShader(device, fragmentShader));

    const SDL_GPUColorTargetDescription colorTargetDescription = {
        .format = SDL_GetGPUSwapchainTextureFormat(device, window),
        .blend_state = {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
        },
    };

    const SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo = {
        .vertex_shader = vertexShader,
        .fragment_shader = fragmentShader,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {.fill_mode = SDL_GPU_FILLMODE_FILL},
        .target_info = {
            .color_target_descriptions = &colorTargetDescription,
            .num_color_targets = 1,
        },
    };

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineCreateInfo);
    if (!pipeline)
    {
        SDL_Log("SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    SDL_ReleaseGPUGraphicsPipeline(device, sPipeline);
    sPipeline = pipeline;

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
}

static void ProcessEvent(SDL_Window* window, const SDL_Event& event)
{
    DEBUG_ASSERT(window);

    switch (event.type)
    {
    case SDL_EVENT_QUIT:
        sShouldWindowClose = true;
        break;
    case SDL_EVENT_KEY_DOWN:
        sKeys[event.key.scancode] = 1;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        ProcessMouse(window);
        break;
    case SDL_EVENT_KEY_UP:
        sKeys[event.key.scancode] = 0;
        break;
    }
}

static void ProcessInput(SDL_Window* window, SDL_GPUDevice* device, f32 deltaTime)
{
    DEBUG_ASSERT(window);
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(deltaTime > 0.0f);

    if (sKeys[SDL_SCANCODE_W])
    {
        sCamera.Move(Camera::MoveDirection::Forward, deltaTime);
    }
    if (sKeys[SDL_SCANCODE_S])
    {
        sCamera.Move(Camera::MoveDirection::Backward, deltaTime);
    }
    if (sKeys[SDL_SCANCODE_D])
    {
        sCamera.Move(Camera::MoveDirection::Right, deltaTime);
    }
    if (sKeys[SDL_SCANCODE_A])
    {
        sCamera.Move(Camera::MoveDirection::Left, deltaTime);
    }
    if (sKeys[SDL_SCANCODE_Z])
    {
        sCamera.Move(Camera::MoveDirection::Down, deltaTime);
    }
    if (sKeys[SDL_SCANCODE_X])
    {
        sCamera.Move(Camera::MoveDirection::Up, deltaTime);
    }

    if (sKeys[SDL_SCANCODE_PERIOD])
    {
        sKeys[SDL_SCANCODE_PERIOD] = 0;
    }

    if (sKeys[SDL_SCANCODE_ESCAPE])
    {
        sKeys[SDL_SCANCODE_ESCAPE] = 0;
        sShouldWindowClose = true;
    }

    if (sKeys[SDL_SCANCODE_R])
    {
        sKeys[SDL_SCANCODE_R] = 0;
        if (!RecompilePipelines(device, window))
        {
            fprintf(stderr, "renderer: pipeline recompilation failed\n");
        }
    }
}

int main()
{
#ifdef __linux__
    if (!SDL_SetHint("SDL_VIDEO_DRIVER", "x11"))
    {
        fprintf(
            stderr,
            "WARNING: I've had some strange bugs on Wayland, including hangs, segfaults on "
            "cleanup (glfw), also RenderDoc doesn't work on Wayland. It seems that using X11 "
            "(or Xwayland) is better for now.\n"
        );
        SDL_Log("SDL_SetHint(\"SDL_VIDEO_DRIVER\", \"x11\" failed: %s", SDL_GetError());
        return 1;
    }
#endif

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL error: %s", SDL_GetError());
        return 1;
    }

    const bool debugMode = true;

    SDL_GPUDevice* const device
        = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, debugMode, "direct3d12");
    if (!device)
    {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* const window = SDL_CreateWindow(
        "demo",
        800,
        600,
        SDL_WINDOW_MOUSE_GRABBED | SDL_WINDOW_MOUSE_RELATIVE_MODE | SDL_WINDOW_FULLSCREEN
    );
    if (!window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    (void)SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    (void)SDL_SetWindowRelativeMouseMode(window, true);

    if (!SDL_ClaimWindowForGPUDevice(device, window))
    {
        SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return 1;
    }

    if (!RecompilePipelines(device, window))
    {
        return 1;
    }
    DEFER(SDL_ReleaseGPUGraphicsPipeline(device, sPipeline));

    sCamera.mPosition = {0.0f, 1.0f, 0.0f};
    sCamera.mWorldUp = {0.0f, 1.0f, 0.0f};
    sCamera.mSpeed = 1.0f;
    sCamera.mMouseSensitivity = 0.001f;
    sCamera.mPitchClamp = glm::radians(89.0f);
    sCamera.UpdateVectors();

    u64 performanceCounter = SDL_GetPerformanceCounter();
    const f64 performancePeriod = 1.0 / f64(SDL_GetPerformanceFrequency());
    u64 lastPerformanceCounter = performanceCounter;

    while (!sShouldWindowClose)
    {
        lastPerformanceCounter = performanceCounter;
        performanceCounter = SDL_GetPerformanceCounter();
        const f64 deltaTime = f64(performanceCounter - lastPerformanceCounter) * performancePeriod;
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            ProcessEvent(window, event);
        }
        ProcessInput(window, device, f32(deltaTime));

        SDL_GPUCommandBuffer* const cmd = SDL_AcquireGPUCommandBuffer(device);
        if (!cmd)
        {
            SDL_Log("SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            return 1;
        }

        SDL_GPUTexture* swapchainTexture = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(
                cmd,
                window,
                &swapchainTexture,
                nullptr,
                nullptr
            ))
        {
            SDL_Log("SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
            return 1;
        }

        if (swapchainTexture)
        {
            UniformData uniformData{};

            int width = 0;
            int height = 0;
            if (!SDL_GetWindowSizeInPixels(window, &width, &height))
            {
                SDL_Log("SDL_GetWindowSizeInPixels failed: %s", SDL_GetError());
                return 1;
            }

            const glm::mat4 worldToView = sCamera.GetViewMatrix();
            const glm::mat4 viewToClip
                = Perspective(glm::radians(70.0f), f32(width) / f32(height), 0.1f);
            uniformData.worldToClip = viewToClip * worldToView;
            uniformData.cameraPositionWorld = sCamera.mPosition;

            SDL_PushGPUVertexUniformData(cmd, 0, &uniformData, sizeof(uniformData));
            SDL_PushGPUFragmentUniformData(cmd, 0, &uniformData, sizeof(uniformData));

            const f32 clearColor = f32(0x3f) / 255.0f;

            const SDL_GPUColorTargetInfo colorTargetInfo = {
                .texture = swapchainTexture,
                .clear_color = {clearColor, clearColor, clearColor, 1.0f},
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_STORE,
            };

            SDL_GPURenderPass* const renderPass
                = SDL_BeginGPURenderPass(cmd, &colorTargetInfo, 1, nullptr);

            SDL_BindGPUGraphicsPipeline(renderPass, sPipeline);

            SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0);

            SDL_EndGPURenderPass(renderPass);
        }

        SDL_SubmitGPUCommandBuffer(cmd);
    }

    return 0;
}
