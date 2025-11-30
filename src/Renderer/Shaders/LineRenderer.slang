#include "Common.slang"

struct LineData
{
    // float3 position1;
    // float color1;
    // float3 position2;
    // float color2;
    float4 data[2];
};
[[vk::binding(1)]]
StructuredBuffer<float4> lineData;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;

    float4 data = lineData[vertexId];
    output.color = UnpackFloatToFloat3(data.w);
    data.w = 1.0;
    if (uniformData.perspectiveChosen == 0)
    {
        output.position = mul(uniformData.worldToClip, data);
    }
    else
    {
        output.position =
            mul(uniformData.shadow.worldToClip[uniformData.perspectiveChosen - 1], data);
    }

    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input)
{
    return float4(input.color, 1.0);
}
