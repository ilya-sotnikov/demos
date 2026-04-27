#pragma once

#include "Common.hlsli"

[[vk::binding(0)]]
ConstantBuffer<UniformData> uniformBuffer;
[[vk::binding(1)]]
StructuredBuffer<uint32_t> drawIndicesBuffer;
[[vk::binding(2)]]
StructuredBuffer<DrawData> drawDataBuffer;
[[vk::binding(3)]]
StructuredBuffer<Vertex> vertexBuffer;

[[vk::push_constant]]
PushConstantsShadow pushConstants;

struct VertexOutput
{
    float4 positionClip : SV_Position;
};

