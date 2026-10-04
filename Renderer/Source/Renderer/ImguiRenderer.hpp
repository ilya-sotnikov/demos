#pragma once

#include "../Common.hpp"
#include "../Math/Types.hpp"
#include "RHI/RHI.hpp"
#include "Shaders/SharedConfig.hlsli"
#include "Shaders/SharedDef.hlsli"

#include "../Math/Types.hpp"

struct SDL_Window;

struct ImguiRenderer {
    struct PushConstantBlock {
        Vec2 scale;
        Vec2 translate;
    };

    RHI::Texture mFontTexture;
    RHI::Sampler mFontSampler;
    RHI::Pipeline mPipeline;

    struct Frame {
        RHI::Buffer vertexBuffer;
        RHI::Buffer indexBuffer;
        u64 vertexBufferSize;
        u64 indexBufferSize;
        int vertexCount;
        int indexCount;
    } mFrame[RHI::FRAMES_IN_FLIGHT];

    bool Init(SDL_Window* window, RHI::Format colorFormat);
    void Cleanup();
    void UpdateVertexIndexBuffers(u32 frameIndex);
    void StartNewFrame() const;
    void Render(RHI::CommandBuffer cb, u32 frameIndex);
};
