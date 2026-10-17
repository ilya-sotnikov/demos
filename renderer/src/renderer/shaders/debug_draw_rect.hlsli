#pragma once

#include "common.hlsli"
#include "debug_draw.hlsli"

struct VertexOutput {
    float4 position_clip : SV_Position;
    nointerpolation float4 lbrt_screen : LBRT_SCREEN;
    nointerpolation uint32_t color : COLOR;
};

[[vk::binding(0)]]
ConstantBuffer<UniformData> uniform_buffer;
