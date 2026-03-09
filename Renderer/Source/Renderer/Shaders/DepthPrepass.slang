#include "Common.slang"

struct VertexOutput
{
    float4 positionClip : SV_Position;
}

[shader("vertex")]
void VertexMain(
    uint vertexId : SV_VertexID,
    uint vertexOffset : SV_StartVertexLocation,
    uint rawDrawIdx : SV_DrawIndex,
    uniform PushConstants pushConstants,
    out VertexOutput output
)
{
    const uint drawIdx = pushConstants.drawCmds2[rawDrawIdx].idx;
    const DrawData drawData = pushConstants.drawData[drawIdx];
    const Vertex vertex = pushConstants.vertices[vertexId + vertexOffset];

    const float4 positionWorld =
        mul(drawData.localToWorld, float4(vertex.px, vertex.py, vertex.pz, 1.0));

    output.positionClip = mul(pushConstants.uniformData.worldToClip, positionWorld);
}

[shader("fragment")]
void FragmentMain()
{
    // Empty.
}
