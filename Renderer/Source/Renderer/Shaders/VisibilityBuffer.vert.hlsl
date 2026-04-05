#include "Common.hlsli"
#include "Math.hlsli"
#include "VisibilityBuffer.hlsli"

// https://static.graphicsprogrammingconference.com/public/2025/slides/visibility-buffer-and-deferred-rendering-in-doom/Lazarek-Hammer-visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages.pdf

void Main(
    uint vertexId : SV_VertexID,
    // Dummy semantics just to please the compiler.
    [[vk::builtin("DrawIndex")]] uint rawDrawIdx : DrawIndex,
    out VertexOutput output
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
