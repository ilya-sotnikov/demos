#include "common.hlsli"
#include "imgui.hlsli"

float4 main(VertexOutput input) : SV_Target
{
    return input.color * fontTexture.Sample(fontSampler, input.uv);
}
