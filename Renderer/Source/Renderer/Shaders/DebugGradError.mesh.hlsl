#include "Common.hlsli"
#include "Math.hlsli"
#include "DebugGradError.hlsli"

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

    const float3 pos = float3(float(vertex.px), float(vertex.py), float(vertex.pz));
    const float4 positionWorld = float4(
        QuatRotate(drawData.orientation, pos) * drawData.scale + drawData.position,
        1.0
    );

    output.positionClip = mul(uniformBuffer.worldToClip, positionWorld);
    output.uv = float2(float(vertex.u), float(vertex.v));
    output.rawDrawIdx = rawDrawIdx;
}
