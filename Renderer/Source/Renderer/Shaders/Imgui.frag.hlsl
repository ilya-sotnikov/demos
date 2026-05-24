#include "Common.hlsli"
#include "Imgui.hlsli"

float4 Main(VertexOutput input) : SV_Target
{
    return input.color * fontTexture.Sample(fontSampler, input.uv);
}
