#include "common.h.slang"
#include "math.h.slang"

Texture2D resolved_render_texture;
SamplerState linear_sampler;

struct VertexOutput {
    float4 position_clip : SV_Position;
    float2 uv;
};

[shader("vertex")]
void main_vert(uint vertex_id : SV_VulkanVertexID, out VertexOutput output)
{
    // Fullscreen triangle.
    output.uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.position_clip = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

[shader("fragment")]
float4 main_frag(VertexOutput input) : SV_Target
{
    float3 color = resolved_render_texture.Sample(linear_sampler, input.uv).rgb;

    return float4(tonemap_hejl_burgess_dawson(color), 1.0f);
}