#include "common.hlsli"
#include "math.hlsli"

// Referenced this:
// Robust Screen Space Ambient Occlusion in 1 ms in 1080p on PS4, Wojciech Sterna, GPU Zen.
// https://www.4rknova.com/blog/2017/01/01/vogel
// https://research.nvidia.com/sites/default/files/pubs/2011-08_The-Alchemy-Screen-space/paper.pdf

[[vk::push_constant]]
PushConstantsSSAO push_constants;

ConstantBuffer<UniformData> uniform_buffer;
StructuredBuffer<uint32_t> draw_indices_early_buffer;
StructuredBuffer<uint32_t> draw_indices_late_buffer;
StructuredBuffer<DrawIndexedIndirectCommand> draw_cmd_early_buffer;
StructuredBuffer<DrawIndexedIndirectCommand> draw_cmd_late_buffer;
StructuredBuffer<DrawData> draw_data_buffer;
StructuredBuffer<uint32_t> index_buffer;
StructuredBuffer<Vertex> vertex_buffer;

SamplerState nearest_sampler;
Texture2D<float> depth_view_texture;
Texture2D<uint2> visibility_texture;
[[vk::image_format("r8")]]
RWTexture2D<float> out_texture;

static const float MAX_RADIUS_SCREEN = 0.1f;
static const float RADIUS_WORLD = 0.3f;
static const int SAMPLES_COUNT = 16;

float2 vogel_disk_offset(int sample_idx, float phi) {
    const float GOLDEN_ANGLE = 2.4f;

    const float r = sqrt(float(sample_idx) + 0.5f) / sqrt(SAMPLES_COUNT);
    const float theta = sample_idx * GOLDEN_ANGLE + phi;

    float sin_theta;
    float cos_theta;
    sincos(theta, sin_theta, cos_theta);

    return float2(r * cos_theta, r * sin_theta);
}

float3 calc_pos_view(float2 uv) {
    float3 pos = float3(uv, -1.0f);
    pos.xy = pos.xy * 2.0f - 1.0f;
    pos.y *= -1.0f;
    pos.xy *= uniform_buffer.view_to_clip_inv0011;
    pos *= -depth_view_texture.SampleLevel(nearest_sampler, uv, 0);
    return pos;
}

[numthreads(RENDERER_SSAO_WORKGROUP_SIZE_X, RENDERER_SSAO_WORKGROUP_SIZE_Y, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const uint2 texture_size = uint2(push_constants.width, push_constants.height);

    if (dtid.x >= texture_size.x || dtid.y >= texture_size.y) {
        return;
    }

    // TODO: unjitter uv or something, TAA messes it up.

    // TODO: reconstructing normals from the depth buffer may be faster
    // and usable in forward pipelines:
    // https://turanszkij.wpcomstaging.com/2019/09/improved-normal-reconstruction-from-depth/
    const uint2 visibility_data = visibility_texture[dtid.xy * 2];
    uint raw_draw_idx = visibility_data.y;

    const bool is_cull_late = raw_draw_idx & 0x80000000;
    raw_draw_idx &= 0x7fffffff;

    if (raw_draw_idx == 0) {
        out_texture[dtid.xy] = 0.0f;
        return;
    }

    --raw_draw_idx;

    const uint triangle_idx = visibility_data.x;

    uint draw_idx;
    if (is_cull_late) {
        draw_idx = draw_indices_late_buffer[raw_draw_idx];
    } else {
        draw_idx = draw_indices_early_buffer[raw_draw_idx];
    }

    DrawIndexedIndirectCommand draw_cmd;
    if (is_cull_late) {
        draw_cmd = draw_cmd_late_buffer[raw_draw_idx];
    } else {
        draw_cmd = draw_cmd_early_buffer[raw_draw_idx];
    }

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

    const float2 texture_size_inv = push_constants.texture_size_inv;

    const float2 pixel_uv = (dtid.xy + 0.5f) * texture_size_inv;
    float2 pixel_ndc = pixel_uv * 2.0f - 1.0f;
    pixel_ndc.y *= -1.0f;

    const BarycentricData bary_data = calc_barycentric_data(
        pos_clip0,
        pos_clip1,
        pos_clip2,
        pixel_ndc,
        2.0f * texture_size_inv
    );

    const float3 pos_view = calc_pos_view(pixel_uv);

    const float3 n0 = unpack_normal_octahedral(unpack_rg8snorm_to_float2(v0.normal));
    const float3 n1 = unpack_normal_octahedral(unpack_rg8snorm_to_float2(v1.normal));
    const float3 n2 = unpack_normal_octahedral(unpack_rg8snorm_to_float2(v2.normal));

    const InterpolatedData3D interp_normal = interpolate_3d(bary_data, n0, n1, n2);

    const float3 normal_world = quat_rotate(draw_data.orientation, normalize(interp_normal.c));
    const float3 normal_view = mul(uniform_buffer.world_to_view, float4(normal_world, 0.0f)).xyz;

    const float noise_pixel = interleaved_gradient_noise(dtid.x, dtid.y);

    float2 radius_screen = RADIUS_WORLD / pos_view.z;
    // To prevent radius_screen getting too large for pixels that are close to the camera.
    radius_screen = min(radius_screen, MAX_RADIUS_SCREEN);
    // Vogel disk returns samples in the [0, 1] unit disk, multiplying by aspect ratio
    // preserves this unit disk, otherwise it would be an ellipse.
    radius_screen.y *= uniform_buffer.aspect;

    float occlusion = 0.0f;

    for (int i = 0; i < SAMPLES_COUNT; ++i) {
        const float2 sample_offset = vogel_disk_offset(i, 2.0f * M_PIf * noise_pixel);
        const float2 sample_uv = pixel_uv + radius_screen * sample_offset;
        const float3 sample_view = calc_pos_view(sample_uv);

        const float3 v = sample_view - pos_view;

        // Tweak beta to reduce self-shadowing.
        const float beta = 1e-3f;
        occlusion += max(0.0f, dot(v, normal_view) + beta * pos_view.z) / (dot(v, v) + 1e-5f);
    }

    occlusion = pow(1.0f - saturate(occlusion / SAMPLES_COUNT), 2);

    out_texture[dtid.xy] = occlusion;
}
