#include "Common.slang"
#include "Math.slang"

Texture2D resolvedRenderImage;
SamplerState linearSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv;
};

[shader("vertex")]
void VertexMain(uint vertexId : SV_VertexID, out VertexOutput output)
{
    // Fullscreen triangle.
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input) : SV_Target
{
    float3 color = resolvedRenderImage.Sample(linearSampler, input.uv).rgb;

    return float4(TonemapHejlBurgessDawson(color), 1.0);
}
