#include "Common.hlsli"
#include "Imgui.hlsli"

float4 Main(VertexOutput input) : SV_Target
{
    return input.color * fontImage.Sample(fontSampler, input.uv);
}
