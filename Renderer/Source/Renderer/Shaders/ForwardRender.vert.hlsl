#include "Common.hlsli"
#include "Math.hlsli"
#include "ForwardRender.hlsli"

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

    output.positionWorld = positionWorld.xyz;
    output.positionClip = mul(uniformBuffer.worldToClip, positionWorld);
    output.normalWorld = QuatRotate(
        drawData.orientation,
        UnpackNormalOctahedral(UnpackRG8SnormToFloat2(vertex.normal))
    );
    output.tangent = float4(
        UnpackNormalOctahedral(UnpackRG8SnormToFloat2(uint16_t(vertex.tangent))),
        (vertex.tangent & (1U << 31)) > 0 ? 1.0 : -1.0
    );
    output.uv = float2(float(vertex.u), float(vertex.v));
    output.drawIdx = drawIdx;
}
