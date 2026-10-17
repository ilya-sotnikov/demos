#pragma once

#include "common.hlsli"

[[vk::binding(0)]]
ConstantBuffer<UniformData> uniform_buffer;
[[vk::binding(1)]]
ConstantBuffer<ShadowPassData> shadow_uniform_buffer;
[[vk::binding(2)]]
StructuredBuffer<uint32_t> draw_indices_buffer;
[[vk::binding(3)]]
StructuredBuffer<DrawData> draw_data_buffer;
[[vk::binding(4)]]
StructuredBuffer<Vertex> vertex_buffer;

[[vk::push_constant]]
PushConstantsShadow push_constants;

struct VertexOutput
{
    float4 position_clip : SV_Position;
};

