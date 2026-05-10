#include "Common.hlsli"

SamplerState nearestSampler;

Texture2DArray<float> shadowImage;
RWTexture2D<float> outImage;

// Downsampling x4 with ESM filtering.
// https://jankautz.com/publications/esm_gi08.pdf
// As explained in:
// https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf

[numthreads(RENDERER_SHADOW_ESM_WORKGROUP_SIZE_X, RENDERER_SHADOW_ESM_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= RENDERER_SHADOW_MAP_DIMENSIONS || dtid.y >= RENDERER_SHADOW_MAP_DIMENSIONS)
    {
        return;
    }

    float4 result = 0.0;

    const float3 coord = float3(
        (dtid.xy + 0.5) / RENDERER_SHADOW_MAP_DIMENSIONS,
        RENDERER_SHADOW_MAP_CASCADE_COUNT - 1
    );

    result += exp(shadowImage.GatherRed(nearestSampler, coord, int2(0, 0)) * RENDERER_ESM_EXPONENT);
    result += exp(shadowImage.GatherRed(nearestSampler, coord, int2(2, 0)) * RENDERER_ESM_EXPONENT);
    result += exp(shadowImage.GatherRed(nearestSampler, coord, int2(0, 2)) * RENDERER_ESM_EXPONENT);
    result += exp(shadowImage.GatherRed(nearestSampler, coord, int2(2, 2)) * RENDERER_ESM_EXPONENT);

    outImage[dtid.xy / 4] = dot(result, 1.0 / 16.0);
}
