#include "common.hlsli"
#include "imgui.hlsli"
#include "math.hlsli"

[[vk::push_constant]]
PushConstantsImgui push_constants;

void main(uint vertex_id : SV_VertexID, out VertexOutput output) {
    const ImguiVertex vertex = vertex_buffer[vertex_id];
    output.position =
        float4(vertex.position * push_constants.scale + push_constants.translate, 0.0f, 1.0f);
    output.uv = vertex.uv;
    output.color = unpack_rgba8unorm_to_float4(vertex.color);
}
