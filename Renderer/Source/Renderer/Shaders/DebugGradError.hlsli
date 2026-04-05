#pragma once

#include "Common.hlsli"

struct VertexOutput
{
    float4 positionClip : SV_Position;
    float2 uv : TexCoord0;
    nointerpolation uint rawDrawIdx : RawDrawIdx;
};

[[vk::binding(0)]]
ConstantBuffer<UniformData> uniformBuffer;
[[vk::binding(1)]]
StructuredBuffer<uint32_t> drawIndicesBuffer;
[[vk::binding(2)]]
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdBuffer;
[[vk::binding(3)]]
StructuredBuffer<DrawData> drawDataBuffer;
[[vk::binding(4)]]
StructuredBuffer<uint32_t> indexBuffer;
[[vk::binding(5)]]
StructuredBuffer<Vertex> vertexBuffer;
