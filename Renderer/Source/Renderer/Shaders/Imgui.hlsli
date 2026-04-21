#pragma once

#include "Common.hlsli"

[[vk::binding(0)]]
Texture2D fontImage;
[[vk::binding(1)]]
SamplerState fontSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TexCoord0;
    float4 color : Color0;
};
