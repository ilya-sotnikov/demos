#include "common.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"
#include "sky.hlsli"

// https://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/

ConstantBuffer<UniformData> uniform_buffer;
ConstantBuffer<ShadowPassData> shadow_uniform_buffer;
StructuredBuffer<uint32_t> draw_indices_early_buffer;
StructuredBuffer<uint32_t> draw_indices_late_buffer;
StructuredBuffer<DrawIndexedIndirectCommand> draw_cmd_early_buffer;
StructuredBuffer<DrawIndexedIndirectCommand> draw_cmd_late_buffer;
StructuredBuffer<DrawData> draw_data_buffer;
StructuredBuffer<uint32_t> index_buffer;
StructuredBuffer<Vertex> vertex_buffer;
StructuredBuffer<Material> material_buffer;
SamplerState linear_sampler;
SamplerState texture_sampler;
SamplerComparisonState shadow_sampler;
SamplerState shadow_pcf_jitter_sampler;

Texture2DArray<float> shadow_texture;
Texture3D shadow_pcf_jitter_texture;
Texture2D<float> fog_texture;
Texture2D<uint2> visibility_texture;
Texture2D<float> ambient_occlusion_texture;
[[vk::image_format("rg16f")]]
RWTexture2D<float2> velocity_texture_rw;
RWTexture2D<float3> render_texture_rw;

// TODO: very messy.
#include "calc_shadow.hlsli"

// NOTE: full bindless is not practical yet since it messes up synchronization validation.
// For now it's only for read-only resources (textures).
[[vk::binding(0, 1)]]
Texture2D textures[];

// http://www.thetenthplanet.de/archives/1180
// https://github.com/ConfettiFX/The-Forge/blob/master/Common_3/Renderer/VisibilityBuffer2/Shaders/FSL/VisibilityBufferShadingUtilities.h.fsl
float3 perturb_normal(
    float3 normal_world,
    float3 normal_sampled,
    float3 ddx_pos_world,
    float3 ddy_pos_world,
    float2 ddx_uv,
    float2 ddy_uv
) {
    // Solve the linear system.
    const float3 ddy_pos_perp = cross(ddy_pos_world, normal_world);
    const float3 ddx_pos_perp = cross(normal_world, ddx_pos_world);
    const float3 T = ddy_pos_perp * ddx_uv.x + ddx_pos_perp * ddy_uv.x;
    const float3 B = ddy_pos_perp * ddx_uv.y + ddx_pos_perp * ddy_uv.y;

    // Construct a scale-invariant frame.
    const float inv_max = rsqrt(max(dot(T, T), dot(B, B)));
    const float3x3 TBN = float3x3(-T * inv_max, B * inv_max, normal_world);

    return normalize(mul(normal_sampled, TBN)); // Mul order is intentional.
}

float4 sample_tex(uint32_t idx, InterpolatedData2D uv) {
    return textures[NonUniformResourceIndex(idx)]
        .SampleGrad(texture_sampler, uv.c, uv.dcdx, uv.dcdy);
}

[numthreads(RENDERER_RENDER_WORKGROUP_SIZE_X, RENDERER_RENDER_WORKGROUP_SIZE_Y, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const int2 render_texture_size = int2(uniform_buffer.render_width, uniform_buffer.render_height);

    if (dtid.x >= render_texture_size.x || dtid.y >= render_texture_size.y) {
        return;
    }

    const uint2 visibility_data = visibility_texture[dtid.xy];
    uint raw_draw_idx = visibility_data.y;

    const bool is_cull_late = raw_draw_idx & 0x80000000;
    raw_draw_idx &= 0x7fffffff;

    float2 pixel_ndc = ((dtid.xy + 0.5f) * uniform_buffer.render_texture_size_inv) * 2.0f - 1.0f;
    pixel_ndc.y *= -1.0f;

    if (raw_draw_idx == 0) {
        const float4 pos = mul(
            uniform_buffer.clip_to_world,
            float4(pixel_ndc, 1.0f, 1.0f)
        );
        const float3 view_direction_world = normalize(pos.xyz / pos.w - uniform_buffer.camera_position);

        render_texture_rw[dtid.xy] = calc_sky(view_direction_world, uniform_buffer.sun_direction_world);
        velocity_texture_rw[dtid.xy] = float2(0.0f, 0.0f);
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
    const Material material = material_buffer[draw_data.material_idx];

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

    const BarycentricData bary_data = calc_barycentric_data(
        pos_clip0,
        pos_clip1,
        pos_clip2,
        pixel_ndc,
        2.0f * uniform_buffer.render_texture_size_inv
    );

    // TODO: will break on anything other than infinite reversed Z.
    const float pixel_depth = RENDERER_NEAR_PLANE * bary_data.interp_inv_w;

    const float4 pixel_world_homo = mul(uniform_buffer.clip_to_world, float4(pixel_ndc, pixel_depth, 1.0f));
    const float3 pixel_world = pixel_world_homo.xyz / pixel_world_homo.w;

    const float2 uv0 = float2(v0.u, v0.v);
    const float2 uv1 = float2(v1.u, v1.v);
    const float2 uv2 = float2(v2.u, v2.v);
    const InterpolatedData2D interp_uv = interpolate_2d(bary_data, uv0, uv1, uv2);

    const float3 n0 = unpack_normal_octahedral(unpack_rg8snorm_to_float2(v0.normal));
    const float3 n1 = unpack_normal_octahedral(unpack_rg8snorm_to_float2(v1.normal));
    const float3 n2 = unpack_normal_octahedral(unpack_rg8snorm_to_float2(v2.normal));

    const InterpolatedData3D interp_normal = interpolate_3d(bary_data, n0, n1, n2);

    const float3 normal_world = normalize(quat_rotate(draw_data.orientation, interp_normal.c));

    float4 albedo = material.albedo_factor;
    if (material.albedo_tex_idx > 0) {
        albedo *= sample_tex(material.albedo_tex_idx, interp_uv);
    }

    float2 metallic_roughness = float2(material.metallic_factor, material.roughness_factor);
    if (material.metallic_roughness_tex_idx > 0) {
        metallic_roughness *= sample_tex(material.metallic_roughness_tex_idx, interp_uv).gr;
    }
    const float metallic = metallic_roughness.r;
    const float roughness = metallic_roughness.g;

    float3 normal = normal_world;

    if (material.normal_tex_idx > 0) {
        const float3 normal_sampled = sample_tex(material.normal_tex_idx, interp_uv).xyz * 2.0f - 1.0f;

        const InterpolatedData3D interp_pos_world =
            interpolate_3d(bary_data, pos_world0, pos_world1, pos_world2);

        normal = perturb_normal(
            normal,
            normal_sampled,
            interp_pos_world.dcdx,
            interp_pos_world.dcdy,
            interp_uv.dcdx,
            interp_uv.dcdy
        );
    }

    const float3 fragment_to_camera = normalize(uniform_buffer.camera_position - pixel_world);

    const float3 half_vec_view_light = normalize(fragment_to_camera - uniform_buffer.sun_direction_world);

    float3 fresnel0 = float3(0.04f, 0.04f, 0.04f); // Good default for dielectrics.
    fresnel0 = lerp(fresnel0, albedo.rgb, metallic);

    const float dot_normal_light = max(0.0f, dot(normal, -uniform_buffer.sun_direction_world));
    const float dot_normal_view = max(0.0f, dot(normal, fragment_to_camera));
    const float dot_normal_half = max(0.0f, dot(normal, half_vec_view_light));
    const float dot_half_view = max(0.0f, dot(half_vec_view_light, fragment_to_camera));

    const float normal_distr = normal_distribution_ggx(dot_normal_half, roughness);
    const float geometry = geometry_smith(dot_normal_view, dot_normal_light, roughness);
    const float3 fresnel = fresnel_schlick(dot_half_view, fresnel0);

    const float3 num = normal_distr * geometry * fresnel;
    // Adding a small constant to prevent division by zero.
    const float3 denom = 4.0f * dot_normal_view * dot_normal_light + 0.0001f;
    const float3 specular = num / denom;

    const float3 k_specular = fresnel;
    float3 k_diffuse = 1.0f - k_specular;
    k_diffuse *= 1.0f - metallic;

    const float3 radiance_out =
        (k_diffuse * albedo.rgb / M_PIf + specular) * dot_normal_light * uniform_buffer.sun_intensity;

    float3 position_shadow;

    // Choosing cascade not by split distances, but by determining the smallest
    // cascade that contains this fragment.
    // TODO: somehow optimize? Maybe even ditch the idea above and choose by split distances.
    // TODO: better offsetting.
    const float normal_offset_scale =
        saturate(1.0 - dot(-uniform_buffer.sun_direction_world, normal_world))
        * shadow_uniform_buffer.normal_offset;
    const float3 scaled_normal_world = normal_world * normal_offset_scale;
    int cascade_idx = -1;
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i) {
        // Leaving a small room for PCF kernel, to avoid sampling outside the cascade.
        // TODO: hacky.
        const float MAX_VALUE = 0.99f;

        const float4 shadow_offset = float4(
            scaled_normal_world * shadow_uniform_buffer.texel_sizes[i],
            0.0f
        );
        position_shadow = mul(
            shadow_uniform_buffer.world_to_clip[i],
            float4(pixel_world, 1.0f) + shadow_offset
        ).xyz;
        position_shadow.z += shadow_uniform_buffer.constant_offset / shadow_uniform_buffer.texel_sizes[i];
        position_shadow.xy = position_shadow.xy * 0.5f + 0.5f;

        const float min_coord = min(position_shadow);
        const float max_coord = max(position_shadow);

        if ((min_coord >= 0.0f) && (max_coord <= MAX_VALUE)) {
            cascade_idx = i;
            break;
        }
    }

    const float shadow = cascade_idx == -1 ? 1.0f :
        calc_shadow(
            shadow_uniform_buffer.enable_pcf,
            shadow_uniform_buffer.pcf_kernel_scale,
            shadow_uniform_buffer.pcf_kernel_cascade_scales[cascade_idx],
            position_shadow,
            float2(dtid.xy),
            dot_normal_light,
            cascade_idx
        );

    float ambient_occlusion = 1.0f;
    if (uniform_buffer.enable_ssao) {
        ambient_occlusion = ambient_occlusion_texture[dtid.xy];
    }

    const float3 ambient = albedo.rgb * uniform_buffer.ambient_intensity * ambient_occlusion;

    float fog_factor = 0.0f;
    if (uniform_buffer.enable_fog) {
        fog_factor = fog_texture.SampleLevel(
            linear_sampler,
            (dtid.xy + 0.5f) * uniform_buffer.render_texture_size_inv,
            0
        );
    }

    float3 color = (
        ambient * (-uniform_buffer.sun_direction_world.y) +
        radiance_out * shadow +
        fog_factor
    ) * uniform_buffer.sun_color;

    switch (uniform_buffer.render_mode) {
    case RENDER_MODE_AMBIENT_OCCLUSION:
        color = ambient_occlusion;
        break;
    }

    render_texture_rw[dtid.xy] = color;

    // TODO: support movable objects (double-buffered draw data).
    const float3 prev_pixel_world = pixel_world;
    const float4 prev_pixel_clip = mul(uniform_buffer.prev_world_to_clip, float4(prev_pixel_world, 1.0f));
    const float2 prev_pixel_ndc = prev_pixel_clip.xy / prev_pixel_clip.w;

    float2 velocity =
        (pixel_ndc - uniform_buffer.taa_jitter) - (prev_pixel_ndc - uniform_buffer.prev_taa_jitter);
    velocity.y *= -1.0f;

    // TODO: motion vectors only for dynamic objects.
    velocity_texture_rw[dtid.xy] = velocity;
}
