#pragma once

#include "../Common.hpp"
#include "RHI/RHI.hpp"
#include "Shaders/SharedConfig.hlsli"
#include "../Math/Types.hpp"
#include "Shaders/SharedDef.hlsli"

#include "../Math/Types.hpp"

struct SDL_Window;

struct ImguiRenderer
{
    struct PushConstantBlock
    {
        Vec2 scale;
        Vec2 translate;
    };

    RHI::TextureHandle mFontTexture;
    RHI::SamplerHandle mFontSampler;
    RHI::PipelineHandle mPipeline;

    struct Frame
    {
        RHI::BufferHandle vertexBuffer;
        RHI::BufferHandle indexBuffer;
        u64 vertexBufferSize;
        u64 indexBufferSize;
        int vertexCount;
        int indexCount;
    } mFrame[RHI::FRAMES_IN_FLIGHT];

    bool Init(SDL_Window* window, RHI::Format colorFormat);
    void Cleanup();
    bool UpdateVertexIndexBuffers(u32 frameIndex);
    void StartNewFrame() const;
    bool Render(RHI::CommandBufferHandle cb, u32 frameIndex);
};
