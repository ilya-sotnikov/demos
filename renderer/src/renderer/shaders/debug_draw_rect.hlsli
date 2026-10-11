#pragma once

#include "common.hlsli"
#include "debug_draw.hlsli"

struct VertexOutput
{
    float4 positionClip : SV_Position;
    nointerpolation float4 lbrtScreen : Lbrt0;
    nointerpolation uint32_t color : Color0;
};

[[vk::binding(0)]]
ConstantBuffer<UniformData> uniformBuffer;
