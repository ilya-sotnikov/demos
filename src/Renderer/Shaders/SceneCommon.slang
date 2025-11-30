#pragma once

struct Vertex
{
    float3 position;
    float3 normal;
};
[[vk::binding(1)]]
StructuredBuffer<Vertex> vertices;

struct DrawData
{
    float4x4 localToWorld;
    float3x3 localToWorldNormal;
    float3 color;
};
[[vk::binding(2)]]
StructuredBuffer<DrawData> drawData;
