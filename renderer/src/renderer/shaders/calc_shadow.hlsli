#pragma once

#include "common.hlsli"

static float sample_shadow(float3 position_shadow, uint cascade_idx) {
    return shadow_texture.SampleCmpLevel(
        shadow_sampler,
        float3(position_shadow.xy, cascade_idx), position_shadow.z,
        0
    ).r;
}

// GPU gems 2, Chapter 17, Efficient Soft-Edged Shadows Using Pixel Shader Branching, Yury Uralsky.
static float calc_shadow(
    uint32_t enable_pcf,
    float pcf_kernel_scale,
    float pcf_kernel_cascade_scale,
    float3 position_shadow,
    float2 position_screen,
    float dot_normal_light,
    int cascade_idx
) {
    float shadow = 0.0f;
    float pcf_kernel_total_scale = pcf_kernel_scale * pcf_kernel_cascade_scale;
    if (enable_pcf == 1 && (pcf_kernel_total_scale != 0.0f)) {
        // By reducing the PCF kernel size for distant cascades we can get
        // achieve seamless shadows without blending.
        // TODO: try to find a function that calculates these coefficients,
        // it should at least depend on shadow map texel sizes.
        const int PCF_SAMPLES_COUNT =
            RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U *
            RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V;
        const float offset_scale = pcf_kernel_total_scale / RENDERER_SHADOW_MAP_DIMENSIONS;

        float3 jitter_coord = float3(position_screen / RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE, 0.0f);
        float3 shadow_coord = position_shadow;

        for (int i = 0; i < 4; ++i) {
            const float4 offset =
                shadow_pcf_jitter_texture.SampleLevel(shadow_pcf_jitter_sampler, jitter_coord, 0);
            jitter_coord.z += 1.0f / (PCF_SAMPLES_COUNT / 2.0f);

            shadow_coord.xy = offset.xy * offset_scale + position_shadow.xy;
            shadow += sample_shadow(shadow_coord, cascade_idx) / 8.0f;

            shadow_coord.xy = offset.zw * offset_scale + position_shadow.xy;
            shadow += sample_shadow(shadow_coord, cascade_idx) / 8.0f;
        }

        // Skip expensive shadow map filtering if either dot_normal_light component is zero
        // or all test samples are 0.0 or 1.0 (completely in umbra or unshadowed).
        if ((dot_normal_light * (shadow - 1.0f) * shadow) != 0.0f) {
            // Probably in the penumbra.

            shadow /= 8.0f;
            for (int i = 0; i < PCF_SAMPLES_COUNT / 2 - 4; ++i) {
                const float4 offset =
                    shadow_pcf_jitter_texture.SampleLevel(shadow_pcf_jitter_sampler, jitter_coord, 0);
                jitter_coord.z += 1.0f / (PCF_SAMPLES_COUNT / 2.0f);

                shadow_coord.xy = offset.xy * offset_scale + position_shadow.xy;
                shadow += sample_shadow(shadow_coord, cascade_idx) / PCF_SAMPLES_COUNT;

                shadow_coord.xy = offset.zw * offset_scale + position_shadow.xy;
                shadow += sample_shadow(shadow_coord, cascade_idx) / PCF_SAMPLES_COUNT;
            }
        }
    } else {
        shadow = sample_shadow(position_shadow, cascade_idx);
    }

    return shadow;
}
