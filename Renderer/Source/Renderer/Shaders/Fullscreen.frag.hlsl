#include "Common.hlsli"
#include "Math.hlsli"
#include "Fullscreen.hlsli"

[[vk::binding(0)]]
Texture2D resolvedRenderTexture;
[[vk::binding(1)]]
SamplerState linearSampler;

float4 Main(VertexOutput input) : SV_Target
{
    float3 color = resolvedRenderTexture.Sample(linearSampler, input.uv).rgb;

    return float4(TonemapHejlBurgessDawson(color), 1.0);
}
