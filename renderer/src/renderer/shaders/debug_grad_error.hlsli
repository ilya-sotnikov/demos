#pragma once

#include "common.hlsli"

struct VertexOutput {
    float4 position_clip : SV_Position;
    float2 uv : UV;
    nointerpolation uint raw_draw_idx : RAW_DRAW_IDX;
};

[[vk::binding(0)]]
ConstantBuffer<UniformData> uniform_buffer;
[[vk::binding(1)]]
StructuredBuffer<uint32_t> draw_indices_buffer;
[[vk::binding(2)]]
StructuredBuffer<DrawIndexedIndirectCommand> draw_cmd_buffer;
[[vk::binding(3)]]
StructuredBuffer<DrawData> draw_data_buffer;
[[vk::binding(4)]]
StructuredBuffer<uint32_t> index_buffer;
[[vk::binding(5)]]
StructuredBuffer<Vertex> vertex_buffer;
