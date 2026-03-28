#include "Common.slang"
#include "Math.slang"

struct VertexOutput
{
    float4 positionClip : SV_Position;
    float2 uv;
    nointerpolation uint rawDrawIdx;
};

ConstantBuffer<UniformData> uniformBuffer;
StructuredBuffer<uint32_t> drawIndicesBuffer;
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdBuffer;
StructuredBuffer<DrawData> drawDataBuffer;
StructuredBuffer<uint32_t> indexBuffer;
StructuredBuffer<Vertex> vertexBuffer;
StructuredBuffer<Material> materialBuffer;

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
    output.uv = float2(vertex.u, vertex.v);
    output.rawDrawIdx = rawDrawIdx;
}

[shader("fragment")]
float4 FragmentMain(
    VertexOutput input,
    uint primitiveId : SV_PrimitiveID,
) : SV_Target
{
    const uint rawDrawIdx = input.rawDrawIdx;
    const uint triangleIdx = primitiveId;
    const uint drawIdx = drawIndicesBuffer[rawDrawIdx];

    const VkDrawIndexedIndirectCommand drawCmd = drawCmdBuffer[rawDrawIdx];
    const DrawData drawData = drawDataBuffer[drawIdx];
    const Material material = materialBuffer[drawData.materialIdx];

    const uint indexBufferIdx0 = drawCmd.firstIndex + (triangleIdx * 3 + 0);
    const uint indexBufferIdx1 = drawCmd.firstIndex + (triangleIdx * 3 + 1);
    const uint indexBufferIdx2 = drawCmd.firstIndex + (triangleIdx * 3 + 2);

    const uint idx0 = indexBuffer[indexBufferIdx0];
    const uint idx1 = indexBuffer[indexBufferIdx1];
    const uint idx2 = indexBuffer[indexBufferIdx2];

    const Vertex v0 = vertexBuffer[drawCmd.vertexOffset + idx0];
    const Vertex v1 = vertexBuffer[drawCmd.vertexOffset + idx1];
    const Vertex v2 = vertexBuffer[drawCmd.vertexOffset + idx2];

    const float3 posLocal0 = float3(v0.px, v0.py, v0.pz);
    const float3 posLocal1 = float3(v1.px, v1.py, v1.pz);
    const float3 posLocal2 = float3(v2.px, v2.py, v2.pz);

    const float3 posWorld0 =
        QuatRotate(drawData.orientation, posLocal0.xyz) * drawData.scale + drawData.position;
    const float3 posWorld1 =
        QuatRotate(drawData.orientation, posLocal1.xyz) * drawData.scale + drawData.position;
    const float3 posWorld2 =
        QuatRotate(drawData.orientation, posLocal2.xyz) * drawData.scale + drawData.position;

    const float4 posClip0 = mul(uniformBuffer.worldToClip, float4(posWorld0, 1.0));
    const float4 posClip1 = mul(uniformBuffer.worldToClip, float4(posWorld1, 1.0));
    const float4 posClip2 = mul(uniformBuffer.worldToClip, float4(posWorld2, 1.0));

    const float2 imageSize = float2(uniformBuffer.swapchainWidth, uniformBuffer.swapchainHeight);

    float2 pixelNdc = (input.positionClip.xy / imageSize) * 2.0 - 1.0;
    pixelNdc.y *= -1.0;

    const BarycentricData baryData = CalcBarycentricData(
        posClip0,
        posClip1,
        posClip2,
        pixelNdc,
        2.0 / imageSize
    );

    const float2 uv0 = float2(v0.u, v0.v);
    const float2 uv1 = float2(v1.u, v1.v);
    const float2 uv2 = float2(v2.u, v2.v);
    const InterpolatedData2D interpUV = Interpolate2D(baryData, uv0, uv1, uv2);

    const float2 hwDdx = ddx(input.uv);
    const float2 hwDdy = ddy(input.uv);

    const float gradError = length(interpUV.dcdx - hwDdx) + length(interpUV.dcdy - hwDdy);
    const float3 color = lerp(
        float3(0.0, 0.0, 1.0),
        float3(1.0, 0.0, 0.0),
        min(gradError, uniformBuffer.gradErrorMax) / uniformBuffer.gradErrorMax
    );

    return float4(color, 1.0);
}
