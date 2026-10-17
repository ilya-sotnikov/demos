#include "common.hlsli"
#include "math.hlsli"
#include "debug_grad_error.hlsli"

void main(
    uint vertex_id : SV_VertexID,
    // Dummy semantics just to please the compiler.
    [[vk::builtin("DrawIndex")]] uint raw_draw_idx : RAW_DRAW_IDX,
    out VertexOutput output
) {
    const uint draw_idx = draw_indices_buffer[raw_draw_idx];
    const DrawData draw_data = draw_data_buffer[draw_idx];
    const Vertex vertex = vertex_buffer[vertex_id];

    const float3 pos = float3(float(vertex.px), float(vertex.py), float(vertex.pz));
    const float4 position_world = float4(
        quat_rotate(draw_data.orientation, pos) * draw_data.scale + draw_data.position,
        1.0f
    );

    output.position_clip = mul(uniform_buffer.world_to_clip, position_world);
    output.uv = float2(float(vertex.u), float(vertex.v));
    output.raw_draw_idx = raw_draw_idx;
}
