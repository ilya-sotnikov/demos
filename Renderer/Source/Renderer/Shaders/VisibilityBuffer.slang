#include "Common.slang"
#include "Math.slang"

// https://static.graphicsprogrammingconference.com/public/2025/slides/visibility-buffer-and-deferred-rendering-in-doom/Lazarek-Hammer-visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages.pdf

struct VertexOutput
{
    float4 positionClip : SV_Position;
    nointerpolation uint rawDrawIdx;
};

ConstantBuffer<UniformData> uniformBuffer;
StructuredBuffer<uint32_t> drawIndicesBuffer;
StructuredBuffer<DrawData> drawDataBuffer;
StructuredBuffer<Vertex> vertexBuffer;

[shader("vertex")]
void VertexMain(
    uint vertexId : SV_VulkanVertexID,
    uint rawDrawIdx : SV_DrawIndex,
    out VertexOutput output,
)
{
    const uint drawIdx = drawIndicesBuffer[rawDrawIdx];
    const DrawData drawData = drawDataBuffer[drawIdx];
    const Vertex vertex = vertexBuffer[vertexId];

    const float3 pos = float3(vertex.px, vertex.py, vertex.pz);
    const float4 positionWorld = float4(
        QuatRotate(drawData.orientation, pos) * drawData.scale + drawData.position,
        1.0
    );

    output.positionClip = mul(uniformBuffer.worldToClip, positionWorld);
    output.rawDrawIdx = rawDrawIdx + 1; // To distinguish background pixels.
}

struct FragmentOutput
{
    uint2 visibilityData : SV_Target0;
}

[shader("fragment")]
void FragmentMain(
    VertexOutput input,
    uint primitiveId : SV_PrimitiveID,
    out FragmentOutput output,
)
{
    output.visibilityData = uint2(primitiveId, input.rawDrawIdx);
}
