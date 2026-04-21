#pragma once

#include "Common.hlsli"

struct VertexOutput
{
    float4 positionClip : SV_Position;
    float3 positionWorld : PositionWorld;
    float3 normalWorld : NormalWorld;
    float4 tangent : Tangent;
    float2 uv : UV;
    nointerpolation uint drawIdx : DrawIdx;
};

[[vk::binding(0)]]
ConstantBuffer<UniformData> uniformBuffer;
[[vk::binding(1)]]
StructuredBuffer<uint32_t> drawIndicesBuffer;
[[vk::binding(2)]]
StructuredBuffer<DrawData> drawDataBuffer;
[[vk::binding(3)]]
StructuredBuffer<Vertex> vertexBuffer;
[[vk::binding(4)]]
StructuredBuffer<Material> materialBuffer;
[[vk::binding(5)]]
SamplerState textureSampler;
[[vk::binding(6)]]
RaytracingAccelerationStructure tlas;

// NOTE: full bindless is not practical yet since it messes up synchronization validation.
// For now it's only for read-only resources (textures).
[[vk::binding(0, 1)]]
Texture2D textures[];
