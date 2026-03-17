#include "Common.slang"
#include "Math.slang"

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

    const float3 pos = float3(vertex.posX, vertex.posY, vertex.posZ);
    const float3 positionWorld =
        QuatRotate(drawData.orientation, pos) * drawData.scale + drawData.position;

    output.positionClip = mul(pushConstants.uniformData.worldToClip, float4(positionWorld, 1.0));
}

[shader("fragment")]
void FragmentMain()
{
    // Empty.
}
