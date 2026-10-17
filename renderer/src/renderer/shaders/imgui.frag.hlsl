#include "common.hlsli"
#include "imgui.hlsli"

float4 main(VertexOutput input) : SV_Target {
    return input.color * font_texture.Sample(font_sampler, input.uv);
}
