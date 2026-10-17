#include "common.hlsli"
#include "math.hlsli"
#include "shadow.hlsli"

void main(
    uint vertex_id : SV_VertexID,
    // Dummy semantics just to please the compiler.
    [[vk::builtin("DrawIndex")]] uint raw_draw_idx : RAW_DRAW_IDX,
    out VertexOutput output
) {
    const uint draw_idx = draw_indices_buffer[raw_draw_idx];
    const DrawData draw_data = draw_data_buffer[draw_idx];
    const Vertex vertex = vertex_buffer[vertex_id];

    const float3 pos = float3(vertex.px, vertex.py, vertex.pz);
    const float3 position_world =
        quat_rotate(draw_data.orientation, pos) * draw_data.scale + draw_data.position;

    output.position_clip = mul(
        shadow_uniform_buffer.world_to_clip[push_constants.shadow_cascade_idx],
        float4(position_world, 1.0f)
    );
}
