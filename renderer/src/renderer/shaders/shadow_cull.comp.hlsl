#include "common.hlsli"
#include "math.hlsli"

ConstantBuffer<UniformData> uniform_buffer;
StructuredBuffer<DrawData> draw_data_buffer;
RWByteAddressBuffer draw_count_buffer;
StructuredBuffer<DrawIndexedIndirectCommand> draw_cmds_buffer1;
RWStructuredBuffer<DrawIndexedIndirectCommand> draw_cmds_buffer2;
RWStructuredBuffer<uint32_t> draw_indices_buffer;

[[vk::push_constant]]
PushConstantsShadow push_constants;

[numthreads(RENDERER_CULL_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const uint draw_idx = dtid.x;

    if (draw_idx >= uniform_buffer.draw_count) {
        return;
    }

    const DrawData draw_data = draw_data_buffer[draw_idx];

    bool visible = draw_data.render_pass_flags == push_constants.render_pass_flags;

    // TODO: ortho frustum cull.

    const uint subgroup_visible_idx = WavePrefixCountBits(visible);
    const uint subgroup_visible_count = WaveActiveCountBits(visible);
    uint base_visible_idx;
    if (WaveIsFirstLane()) {
        draw_count_buffer.InterlockedAdd(0, subgroup_visible_count, base_visible_idx);
    }
    base_visible_idx = WaveReadLaneFirst(base_visible_idx);

    if (visible) {
        const uint visible_idx = base_visible_idx + subgroup_visible_idx;
        const DrawIndexedIndirectCommand draw_cmd = draw_cmds_buffer1[draw_idx];

        DrawIndexedIndirectCommand write_draw_cmd;
        write_draw_cmd.index_count = draw_cmd.index_count;
        write_draw_cmd.instance_count = 1;
        write_draw_cmd.first_index = draw_cmd.first_index;
        write_draw_cmd.vertex_offset = draw_cmd.vertex_offset;
        write_draw_cmd.first_instance = 0;

        draw_cmds_buffer2[visible_idx] = write_draw_cmd;
        draw_indices_buffer[visible_idx] = draw_idx;
    }
}
