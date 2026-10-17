#include "common.hlsli"
#include "math.hlsli"
#include "debug_draw.hlsli"

// Heavily based on from niagara:
// https://github.com/zeux/niagara

[[vk::push_constant]]
PushConstantsCull cull_pass_data;

ConstantBuffer<UniformData> uniform_buffer;
StructuredBuffer<DrawData> draw_data_buffer;
RWByteAddressBuffer draw_count_buffer;
StructuredBuffer<DrawIndexedIndirectCommand> draw_cmds_buffer1;
RWStructuredBuffer<DrawIndexedIndirectCommand> draw_cmds_buffer2;
RWStructuredBuffer<uint32_t> draw_indices_buffer;
RWStructuredBuffer<uint32_t> mesh_primitive_visible_buffer;

SamplerState min_sampler;
Texture2D<float> depth_pyramid_texture;

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere, Michael Mara, Morgan McGuire
// http://jcgt.org/published/0002/02/05/paper.pdf
// Took the implementation from niagara:
// https://github.com/zeux/niagara
// The idea is to simplify the code in this paper, because it's very generic
// and we only care about getting a clip space (uv) AABB of a sphere.
bool project_sphere(
    float3 center,
    float radius,
    float z_near,
    float perspective00,
    float perspective11,
    out float4 aabb
) {
    // If the bounding sphere intersects the near plane, we skip occlusion tests
    // since it simplifies math and in this case the object is probably not occluded anyway.
    if (center.z > -(radius + z_near)) {
        return false;
    }

    const float3 cr = center * radius;
    const float czr2 = center.z * center.z - radius * radius;

    const float vx = sqrt(center.x * center.x + czr2);
    const float minx = (vx * center.x - cr.z) / (vx * center.z + cr.x);
    const float maxx = (vx * center.x + cr.z) / (vx * center.z - cr.x);

    const float vy = sqrt(center.y * center.y + czr2);
    const float miny = (vy * center.y - cr.z) / (vy * center.z + cr.y);
    const float maxy = (vy * center.y + cr.z) / (vy * center.z - cr.y);

    aabb = float4(
        minx * perspective00,
        miny * perspective11,
        maxx * perspective00,
        maxy * perspective11
    );
    aabb = aabb.zyxw * float4(-0.5f, 0.5f, -0.5f, 0.5f) + 0.5f; // Clip space -> uv space.

    return true;
}

[numthreads(RENDERER_CULL_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const uint draw_idx = dtid.x;

    if (draw_idx >= uniform_buffer.draw_count) {
        return;
    }

    const DrawData draw_data = draw_data_buffer[draw_idx];

    // TODO: support translucent materials.
    bool visible = draw_data.render_pass_flags == RENDER_PASS_OPAQUE_BIT;

    // In the early cull pass of 2-pass Hi-Z occlusion culling, we render only
    // mesh primitives that were visible last frame.
    if (CULL_LATE == 0) {
        visible = visible && mesh_primitive_visible_buffer[draw_idx] != 0;
    }

    const float3 sphere_center_world =
        quat_rotate(draw_data.orientation, draw_data.sphere_center) * draw_data.scale + draw_data.position;
    const float3 sphere_center_view = mul(
        uniform_buffer.cull_world_to_view,
        float4(sphere_center_world, 1.0f)
    ).xyz;
    const float sphere_radius = draw_data.scale * draw_data.sphere_radius;

    // View frustum is symmetrical, therefore we can cull against two opposite planes simultaneously.
    visible = visible && sphere_center_view.z * cull_pass_data.frustum_plane_xz -
        abs(sphere_center_view.x) * cull_pass_data.frustum_plane_xx > -sphere_radius;

    visible = visible && sphere_center_view.z * cull_pass_data.frustum_plane_yz -
        abs(sphere_center_view.y) * cull_pass_data.frustum_plane_yy > -sphere_radius;

    visible = visible && sphere_center_view.z - sphere_radius < -RENDERER_NEAR_PLANE;

    if ((CULL_LATE == 1) && visible) {
        float4 aabb; // Screen-space (uv) AABB.
        if (project_sphere(
            sphere_center_view,
            sphere_radius,
            RENDERER_NEAR_PLANE,
            uniform_buffer.view_to_clip[0][0],
            uniform_buffer.view_to_clip[1][1],
            aabb)) {

            const float width = (aabb.z - aabb.x) * cull_pass_data.depth_pyramid_width;
            const float height = (aabb.w - aabb.y) * cull_pass_data.depth_pyramid_height;

            const float mip_level = ceil(log2(max(width, height)));

            // Min reduction sampler (minimum depth of 2x2 texel quad).
            const float depth =
                depth_pyramid_texture.SampleLevel(min_sampler, (aabb.xy + aabb.zw) * 0.5f, mip_level);
            const float depth_sphere = RENDERER_NEAR_PLANE / (-sphere_center_view.z - sphere_radius);

            visible = visible && depth_sphere > depth;

            if (cull_pass_data.draw_cull_aabb) {
                debug_draw_rect(
                    aabb * float4(
                        uniform_buffer.render_width,
                        uniform_buffer.render_height,
                        uniform_buffer.render_width,
                        uniform_buffer.render_height
                    ),
                    COLOR_RED
                );
            }
        }
    }

    // In the early cull pass, we are always writing draw commands (for previously
    // visible mesh primitives).
    // In the late cull pass, we are writing only draw commands for mesh primitives
    // that are visible, but were not visible in the early cull pass.
    // Therefore, if camera did not move between the 2 consecutive frames, visibleCount == 0.
    const bool should_write_draw_data =
        visible && ((CULL_LATE == 0) || (mesh_primitive_visible_buffer[draw_idx] == 0));

    const uint subgroup_visible_idx = WavePrefixCountBits(should_write_draw_data);
    const uint subgroup_visible_count = WaveActiveCountBits(should_write_draw_data);
    uint base_visible_idx;
    if (WaveIsFirstLane()) {
        draw_count_buffer.InterlockedAdd(0, subgroup_visible_count, base_visible_idx);
    }
    base_visible_idx = WaveReadLaneFirst(base_visible_idx);

    if (should_write_draw_data) {
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

    if (CULL_LATE == 1) {
        // On the next frame, this buffer will be used in the early cull pass
        // to draw mesh primitives that were visible last frame.
        // TODO: pack?
        mesh_primitive_visible_buffer[draw_idx] = visible ? 1 : 0;
    }
}

