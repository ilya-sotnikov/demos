#include "common.hlsli"
#include "math.hlsli"
#include "visibility_buffer.hlsli"

// https://static.graphicsprogrammingconference.com/public/2025/slides/visibility-buffer-and-deferred-rendering-in-doom/Lazarek-Hammer-visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages.pdf

[[vk::push_constant]]
PushConstantsVisibilityBuffer push_constants;

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
    const float4 position_world = float4(
        quat_rotate(draw_data.orientation, pos) * draw_data.scale + draw_data.position,
        1.0f
    );

    output.position_clip = mul(uniform_buffer.world_to_clip, position_world);

    uint outraw_draw_idx = (raw_draw_idx + 1) & 0x7fffffff; // +1 to distinguish background pixels.
    // Cull pass writes draw cmds and draw data for early and late passes separately,
    // since atomic operation order is unspecified (or, rather, thread invocation order),
    // draw cmds and draw data order in early and late cull passes is different,
    // therefore renderer needs to distinguish them.
    // TODO: sorting will also work and we'll get frame coherent draw cmds and data. Try it out?
    outraw_draw_idx |= push_constants.cull_late << 31;

    output.raw_draw_idx = outraw_draw_idx;
}
