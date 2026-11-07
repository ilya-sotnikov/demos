#include "common.h.slang"
#include "math.h.slang"

ConstantBuffer<ShadowPassData> shadow_uniform_buffer;
StructuredBuffer<uint32_t> draw_indices_buffer;
StructuredBuffer<DrawData> draw_data_buffer;
StructuredBuffer<Vertex> vertex_buffer;

[[vk::push_constant]]
PushConstantsShadow push_constants;

struct VertexOutput {
    float4 position_clip : SV_Position;
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
    const float3 position_world =
        quat_rotate(draw_data.orientation, pos) * draw_data.scale + draw_data.position;

    output.position_clip = mul(
        shadow_uniform_buffer.world_to_clip[push_constants.shadow_cascade_idx],
        float4(position_world, 1.0f)
    );
}

[shader("fragment")]
void main_frag(VertexOutput input) {
    // Empty.
}
