#include "common.h.slang"
#include "math.h.slang"

// https://static.graphicsprogrammingconference.com/public/2025/slides/visibility-buffer-and-deferred-rendering-in-doom/Lazarek-Hammer-visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages.pdf

ConstantBuffer<UniformData> uniform_buffer;
StructuredBuffer<uint32_t> draw_indices_buffer;
StructuredBuffer<DrawData> draw_data_buffer;
StructuredBuffer<Vertex> vertex_buffer;

[[vk::push_constant]]
PushConstantsVisibilityBuffer push_constants;

struct VertexOutput {
    float4 position_clip : SV_Position;
    nointerpolation uint raw_draw_idx;
};

[shader("vertex")]
void main_vert(
    uint vertex_id : SV_VulkanVertexID,
    uint raw_draw_idx : SV_DrawIndex,
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

struct FragmentOutput {
    uint2 visibility_data : SV_Target0;
};

[shader("fragment")]
void main_frag(
    VertexOutput input,
    uint primitive_id : SV_PrimitiveID,
    out FragmentOutput output
) : SV_Target {
    output.visibility_data = uint2(primitive_id, input.raw_draw_idx);
}