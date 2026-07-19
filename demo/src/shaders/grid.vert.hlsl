#include "grid.hlsli"

ConstantBuffer<UniformData> uniform_buffer : register(b0, space1);

void main(uint vertex_id : SV_VertexID, out VertexOutput output) {
    const float2 positions_xz[] = {
        float2(-1.0, -1.0),
        float2( 1.0, -1.0),
        float2( 1.0,  1.0),
        float2(-1.0,  1.0),
    };
    const int indices[] = {0, 2, 1, 2, 0, 3};

    const int idx = indices[vertex_id];
    const float3 cam_pos_world = uniform_buffer.camera_position_world;
    const float half_size = calc_grid_half_size(cam_pos_world.y);
    const float2 pos = positions_xz[idx] * half_size;
    const float2 grid_pos_world = float2(pos.x + cam_pos_world.x, pos.y + cam_pos_world.z);

    output.pos_world_xz = grid_pos_world;
    output.pos_clip = mul(
        uniform_buffer.world_to_clip,
        float4(grid_pos_world.x, 0.0, grid_pos_world.y, 1.0));
}
