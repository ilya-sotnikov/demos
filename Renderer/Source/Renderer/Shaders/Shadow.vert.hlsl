#include "Common.hlsli"
#include "Math.hlsli"
#include "Shadow.hlsli"

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
    const float3 positionWorld =
        QuatRotate(drawData.orientation, pos) * drawData.scale + drawData.position;

    output.positionClip = mul(
        uniformBuffer.shadow.worldToClip[pushConstants.shadowCascadeIdx],
        float4(positionWorld, 1.0)
    );
}
