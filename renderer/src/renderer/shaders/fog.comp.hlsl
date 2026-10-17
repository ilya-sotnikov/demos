#include "common.hlsli"
#include "math.hlsli"

ConstantBuffer<UniformData> uniform_buffer;
ConstantBuffer<ShadowPassData> shadow_uniform_buffer;

SamplerState nearest_sampler;
Texture2D<float> depth_texture;
Texture2DArray<float> shadow_texture;
[[vk::image_format("r16f")]]
RWTexture2D<float> out_texture;

// TODO: this is a brute force approach, implement something like:
// https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf

static const int RAYMARCH_STEPS = 16;
static const float FOG_INTENSITY = 0.5f;

float3 calc_pos_world(float2 uv) {
    const float depth = depth_texture.SampleLevel(nearest_sampler, uv, 0);

    float2 pos_ndc = uv * 2.0f - 1.0f;
    pos_ndc.y *= -1.0f;

    const float4 pos_world_homo = mul(
        uniform_buffer.clip_to_world,
        float4(pos_ndc, depth, 1.0f)
    );

    return pos_world_homo.xyz / pos_world_homo.w;
}

float calc_shadow(float3 posWorld) {
    // TODO: for now just taking the largest cascade.
    const int CASCADE_IDX = RENDERER_SHADOW_MAP_CASCADE_COUNT - 1;
    float3 pos_shadow = mul(
        shadow_uniform_buffer.world_to_clip[CASCADE_IDX],
        float4(posWorld, 1.0f)
    ).xyz;
    pos_shadow.xy = pos_shadow.xy * 0.5f + 0.5f;

    const float min_shadow_coord = min(pos_shadow);
    const float max_shadow_coord = max(pos_shadow);

    float shadow = 0.0f;

    if ((min_shadow_coord >= 0.0f) && (max_shadow_coord <= 1.0f)) {
        shadow = shadow_texture.SampleLevel(
            nearest_sampler,
            float3(pos_shadow.xy, RENDERER_SHADOW_MAP_CASCADE_COUNT - 1),
            0
        ) < pos_shadow.z ? 1.0f : 0.0f;
    }

    return shadow;
}

// Approximates Mie scattering.
float phase_henyey_greenstein(float march_dir_dot_light_dir, float scatter_factor) {
    const float g = scatter_factor;
    const float nom = 1.0f - g * g;
    const float denom = 4.0f * M_PIf * pow(1.0f + g * g - 2.0f * g * march_dir_dot_light_dir, 1.5f);
    return nom / denom;
}

[numthreads(RENDERER_FOG_WORKGROUP_SIZE_X, RENDERER_FOG_WORKGROUP_SIZE_Y, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const uint2 texture_size = uint2(uniform_buffer.render_width, uniform_buffer.render_height);

    if (dtid.x >= texture_size.x || dtid.y >= texture_size.y) {
        return;
    }

    const float2 uv = (dtid.xy + 0.5f) * uniform_buffer.render_texture_size_inv;

    const float3 ray_start = uniform_buffer.camera_position;
    const float3 ray_end = calc_pos_world(uv);
    const float3 ray_dir = normalize(ray_end - ray_start);
    const float3 ray_step = (ray_end - ray_start) / RAYMARCH_STEPS;

    // We'll dither rays to hide banding artifacts due to low number of raymarching steps.
    float3 sample_pos_world = ray_start + interleaved_gradient_noise(dtid.x, dtid.y) * ray_step;
    float result = 0.0f;

    const float3 sun_dir_world = uniform_buffer.sun_direction_world;

    for (int i = 0; i < RAYMARCH_STEPS; ++i) {
        const float shadow = calc_shadow(sample_pos_world);

        result += phase_henyey_greenstein(dot(ray_dir, sun_dir_world), 0.3f) * shadow * FOG_INTENSITY;

        sample_pos_world += ray_step;
    }

    result /= RAYMARCH_STEPS;

    out_texture[dtid.xy] = result;
}
