#include "common.hlsli"
#include "math.hlsli"
#include "fullscreen.hlsli"

[[vk::binding(0)]]
Texture2D resolved_render_texture;
[[vk::binding(1)]]
SamplerState linear_sampler;

float4 main(VertexOutput input) : SV_Target {
    float3 color = resolved_render_texture.Sample(linear_sampler, input.uv).rgb;

    return float4(tonemap_hejl_burgess_dawson(color), 1.0f);
}
