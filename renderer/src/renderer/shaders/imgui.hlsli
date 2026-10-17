#pragma once

#include "common.hlsli"

struct ImguiVertex {
    float2 position;
    float2 uv;
    uint32_t color;
};

[[vk::binding(0)]]
StructuredBuffer<ImguiVertex> vertex_buffer;
[[vk::binding(1)]]
Texture2D font_texture;
[[vk::binding(2)]]
SamplerState font_sampler;

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : UV;
    float4 color : COLOR;
};
