#include "Common.hlsli"
#include "Math.hlsli"
#include "VisibilityBuffer.hlsli"

// https://static.graphicsprogrammingconference.com/public/2025/slides/visibility-buffer-and-deferred-rendering-in-doom/Lazarek-Hammer-visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages.pdf

[[vk::push_constant]]
PushConstantsVisibilityBuffer pushConstants;

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

    uint outRawDrawIdx = (rawDrawIdx + 1) & 0x7fffffff; // +1 to distinguish background pixels.
    // Cull pass writes draw cmds and draw data for early and late passes separately,
    // since atomic operation order is unspecified (or, rather, thread invocation order),
    // draw cmds and draw data order in early and late cull passes is different,
    // therefore renderer needs to distinguish them.
    // TODO: sorting will also work and we'll get frame coherent draw cmds and data. Try it out?
    outRawDrawIdx |= pushConstants.cullLate << 31;

    output.rawDrawIdx = outRawDrawIdx;
}
