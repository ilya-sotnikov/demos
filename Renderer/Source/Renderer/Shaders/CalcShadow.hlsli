#pragma once

#include "Common.hlsli"

static float SampleShadow(float3 positionShadow, uint cascadeIdx)
{
    return shadowImage.SampleCmpLevel(
        shadowSampler,
        float3(positionShadow.xy, cascadeIdx), positionShadow.z,
        0
    ).r;
}

// GPU gems 2, Chapter 17, Efficient Soft-Edged Shadows Using Pixel Shader Branching, Yury Uralsky.
static float CalcShadow(
    uint32_t enablePcf,
    float pcfKernelScale,
    float pcfKernelCascadeScale,
    float3 positionShadow,
    float2 positionScreen,
    float dotNormalLight,
    int cascadeIdx
)
{
    float shadow = 0.0;
    float pcfKernelTotalScale = pcfKernelScale * pcfKernelCascadeScale;
    if (enablePcf == 1 && (pcfKernelTotalScale != 0.0))
    {
        // By reducing the PCF kernel size for distant cascades we can get
        // achieve seamless shadows without blending.
        // TODO: try to find a function that calculates these coefficients,
        // it should at least depend on shadow map texel sizes.
        const int PCF_SAMPLES_COUNT =
            RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U *
            RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V;
        const float offsetScale = pcfKernelTotalScale / RENDERER_SHADOW_MAP_DIMENSIONS;

        float3 jitterCoord = float3(positionScreen / RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE, 0.0);
        float3 shadowCoord = positionShadow;

        for (int i = 0; i < 4; ++i)
        {
            const float4 offset =
                shadowPcfJitterImage.SampleLevel(shadowPcfJitterSampler, jitterCoord, 0);
            jitterCoord.z += 1.0f / (PCF_SAMPLES_COUNT / 2.0);

            shadowCoord.xy = offset.xy * offsetScale + positionShadow.xy;
            shadow += SampleShadow(shadowCoord, cascadeIdx) / 8.0;

            shadowCoord.xy = offset.zw * offsetScale + positionShadow.xy;
            shadow += SampleShadow(shadowCoord, cascadeIdx) / 8.0;
        }

        // Skip expensive shadow map filtering if either dotNormalLight component is zero
        // or all test samples are 0.0 or 1.0 (completely in umbra or unshadowed).
        if ((dotNormalLight * (shadow - 1.0) * shadow) != 0.0)
        {
            // Probably in the penumbra.

            shadow /= 8.0;
            for (int i = 0; i < PCF_SAMPLES_COUNT / 2 - 4; ++i)
            {
                const float4 offset =
                    shadowPcfJitterImage.SampleLevel(shadowPcfJitterSampler, jitterCoord, 0);
                jitterCoord.z += 1.0f / (PCF_SAMPLES_COUNT / 2.0);

                shadowCoord.xy = offset.xy * offsetScale + positionShadow.xy;
                shadow += SampleShadow(shadowCoord, cascadeIdx) / PCF_SAMPLES_COUNT;

                shadowCoord.xy = offset.zw * offsetScale + positionShadow.xy;
                shadow += SampleShadow(shadowCoord, cascadeIdx) / PCF_SAMPLES_COUNT;
            }
        }
    }
    else
    {
        shadow = SampleShadow(positionShadow, cascadeIdx);
    }

    return shadow;
}
