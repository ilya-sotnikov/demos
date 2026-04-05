#pragma once

#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TexCoord0;
    float4 color : Color0;
};
