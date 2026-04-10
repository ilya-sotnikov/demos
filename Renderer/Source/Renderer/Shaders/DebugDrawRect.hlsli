#pragma once

#include "Common.hlsli"
#include "DebugDraw.hlsli"

struct VertexOutput
{
    float4 positionClip : SV_Position;
    nointerpolation float4 lbrtScreen : Lbrt0;
    nointerpolation uint32_t color : Color0;
};

[[vk::binding(0)]]
ConstantBuffer<UniformData> uniformBuffer;
