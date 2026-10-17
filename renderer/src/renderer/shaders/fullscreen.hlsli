#pragma once

#include "common.hlsli"

struct VertexOutput {
    float4 position_clip : SV_Position;
    float2 uv : UV;
};
