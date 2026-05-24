#pragma once

#include "Common.hlsli"

struct ImguiVertex
{
    float2 position;
    float2 uv;
    uint32_t color;
};

[[vk::binding(0)]]
StructuredBuffer<ImguiVertex> vertexBuffer;
[[vk::binding(1)]]
Texture2D fontTexture;
[[vk::binding(2)]]
SamplerState fontSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TexCoord0;
    float4 color : Color0;
};
