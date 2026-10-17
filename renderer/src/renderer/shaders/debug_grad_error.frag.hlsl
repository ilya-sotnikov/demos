#include "common.hlsli"
#include "math.hlsli"
#include "debug_grad_error.hlsli"

float4 main(VertexOutput input, uint primitive_id : SV_PrimitiveID) : SV_Target {
    const uint raw_draw_idx = input.raw_draw_idx;
    const uint triangle_idx = primitive_id;
    const uint draw_idx = draw_indices_buffer[raw_draw_idx];

    const DrawIndexedIndirectCommand draw_cmd = draw_cmd_buffer[raw_draw_idx];
    const DrawData draw_data = draw_data_buffer[draw_idx];

    const uint index_buffer_idx0 = draw_cmd.first_index + (triangle_idx * 3 + 0);
    const uint index_buffer_idx1 = draw_cmd.first_index + (triangle_idx * 3 + 1);
    const uint index_buffer_idx2 = draw_cmd.first_index + (triangle_idx * 3 + 2);

    const uint idx0 = index_buffer[index_buffer_idx0];
    const uint idx1 = index_buffer[index_buffer_idx1];
    const uint idx2 = index_buffer[index_buffer_idx2];

    const Vertex v0 = vertex_buffer[draw_cmd.vertex_offset + idx0];
    const Vertex v1 = vertex_buffer[draw_cmd.vertex_offset + idx1];
    const Vertex v2 = vertex_buffer[draw_cmd.vertex_offset + idx2];

    const float3 pos_local0 = float3(v0.px, v0.py, v0.pz);
    const float3 pos_local1 = float3(v1.px, v1.py, v1.pz);
    const float3 pos_local2 = float3(v2.px, v2.py, v2.pz);

    const float3 pos_world0 =
        quat_rotate(draw_data.orientation, pos_local0.xyz) * draw_data.scale + draw_data.position;
    const float3 pos_world1 =
        quat_rotate(draw_data.orientation, pos_local1.xyz) * draw_data.scale + draw_data.position;
    const float3 pos_world2 =
        quat_rotate(draw_data.orientation, pos_local2.xyz) * draw_data.scale + draw_data.position;

    const float4 pos_clip0 = mul(uniform_buffer.world_to_clip, float4(pos_world0, 1.0f));
    const float4 pos_clip1 = mul(uniform_buffer.world_to_clip, float4(pos_world1, 1.0f));
    const float4 pos_clip2 = mul(uniform_buffer.world_to_clip, float4(pos_world2, 1.0f));

    const float2 texture_size = float2(uniform_buffer.swapchain_width, uniform_buffer.swapchain_height);

    float2 pixel_ndc = (input.position_clip.xy / texture_size) * 2.0f - 1.0f;
    pixel_ndc.y *= -1.0f;

    const BarycentricData bary_data = calc_barycentric_data(
        pos_clip0,
        pos_clip1,
        pos_clip2,
        pixel_ndc,
        2.0f / texture_size
    );

    const float2 uv0 = float2(v0.u, v0.v);
    const float2 uv1 = float2(v1.u, v1.v);
    const float2 uv2 = float2(v2.u, v2.v);
    const InterpolatedData2D interp_uv = interpolate_2d(bary_data, uv0, uv1, uv2);

    const float2 hw_ddx = ddx_fine(input.uv);
    const float2 hw_ddy = ddy_fine(input.uv);

    const float grad_error = max(
        length(interp_uv.dcdx - hw_ddx) / length(hw_ddx),
        length(interp_uv.dcdy - hw_ddy) / length(hw_ddy)
    );

    // TODO: maybe also uv error?
    const float3 color = lerp(
        float3(0.0f, 0.0f, 1.0f),
        float3(1.0f, 0.0f, 0.0f),
        min(grad_error, uniform_buffer.grad_error_max) / uniform_buffer.grad_error_max
    );

    return float4(color, 1.0f);
}
