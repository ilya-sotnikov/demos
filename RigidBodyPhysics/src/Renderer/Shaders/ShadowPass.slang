#include "Common.slang"
#include "SceneCommon.slang"

struct VertexOutput
{
    float4 position : SV_Position;
};

struct PushConstants
{
    uint shadowCascadeIndex;
};

[shader("vertex")]
VertexOutput vertexMain(
    uint vertexId : SV_VertexID,
    uint vertexOffset : SV_StartVertexLocation,
    uint drawId : SV_DrawIndex,
    uniform PushConstants pushConstants
)
{
    const Vertex vertex = vertices[vertexOffset + vertexId];
    const DrawData data = drawData[drawId];

    const float4x4 localToClip = mul(
        uniformData.shadow.worldToClip[pushConstants.shadowCascadeIndex],
        data.localToWorld
    );

    VertexOutput output;
    output.position = mul(localToClip, float4(vertex.position, 1.0));
    return output;
}

[shader("fragment")]
void fragmentMain(VertexOutput input)
{
    // Empty.
}
