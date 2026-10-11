#pragma once

#include "common.hlsli"

struct VertexOutput
{
    float4 positionClip : SV_Position;
    float2 uv : TexCoord0;
};
