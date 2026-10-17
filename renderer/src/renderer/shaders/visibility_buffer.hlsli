#pragma once

#include "common.hlsli"

[[vk::binding(0)]]
ConstantBuffer<UniformData> uniform_buffer;
[[vk::binding(1)]]
StructuredBuffer<uint32_t> draw_indices_buffer;
[[vk::binding(2)]]
StructuredBuffer<DrawData> draw_data_buffer;
[[vk::binding(3)]]
StructuredBuffer<Vertex> vertex_buffer;

struct VertexOutput
{
    float4 position_clip : SV_Position;
    nointerpolation uint raw_draw_idx : RAW_DRAW_IDX;
};
