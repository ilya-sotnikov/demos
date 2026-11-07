#include "common.h.slang"
#include "math.h.slang"

[[vk::push_constant]]
PushConstantsImgui push_constants;

StructuredBuffer<ImguiVertex> vertex_buffer;
Texture2D font_texture;
SamplerState font_sampler;

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv;
    float4 color;
};

struct ImguiVertex {
    float2 position;
    float2 uv;
    uint32_t color;
};

[shader("vertex")]
void main_vert(uint vertex_id : SV_VulkanVertexID, out VertexOutput output) {
    const ImguiVertex vertex = vertex_buffer[vertex_id];
    output.position =
        float4(vertex.position * push_constants.scale + push_constants.translate, 0.0f, 1.0f);
    output.uv = vertex.uv;
    output.color = unpack_rgba8unorm_to_float4(vertex.color);
}

[shader("fragment")]
float4 main_frag(VertexOutput input) : SV_Target {
    return input.color * font_texture.Sample(font_sampler, input.uv);
}
