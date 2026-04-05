#include "Common.hlsli"
#include "Imgui.hlsli"

[[vk::combinedImageSampler]]
[[vk::binding(0)]]
Texture2D fontImage;

[[vk::combinedImageSampler]]
[[vk::binding(0)]]
SamplerState fontSampler;

float4 Main(VertexOutput input) : SV_Target
{
    return input.color * fontImage.Sample(fontSampler, input.uv);
}
