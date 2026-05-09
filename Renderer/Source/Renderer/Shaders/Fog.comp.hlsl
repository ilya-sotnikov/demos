#include "Common.hlsli"
#include "Math.hlsli"

ConstantBuffer<UniformData> uniformBuffer;

SamplerState nearestSampler;
Texture2D<float> depthImage;
Texture2DArray<float> shadowImage;
[[vk::image_format("r16f")]]
RWTexture2D<float> outImage;

// TODO: this is a brute force approach, implement something like:
// https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf

static const int RAYMARCH_STEPS = 16;
static const float FOG_INTENSITY = 0.5;

float3 CalcPosWorld(float2 uv)
{
    const float depth = depthImage.SampleLevel(nearestSampler, uv, 0);

    float2 posNdc = uv * 2.0 - 1.0;
    posNdc.y *= -1.0;

    const float4 posWorldHomo = mul(
        uniformBuffer.clipToWorld,
        float4(posNdc, depth, 1.0)
    );

    return posWorldHomo.xyz / posWorldHomo.w;
}

float CalcShadow(float3 posWorld)
{
    // TODO: for now just taking the largest cascade.
    const int CASCADE_IDX = RENDERER_SHADOW_MAP_CASCADE_COUNT - 1;
    float3 posShadow = mul(
        uniformBuffer.shadow.worldToClip[CASCADE_IDX],
        float4(posWorld, 1.0)
    ).xyz;
    posShadow.xy = posShadow.xy * 0.5 + 0.5;

    const float minShadowCoord = Min(posShadow);
    const float maxShadowCoord = Max(posShadow);

    float shadow = 0.0;

    if ((minShadowCoord >= 0.0) && (maxShadowCoord <= 1.0))
    {
        shadow = shadowImage.SampleLevel(
            nearestSampler,
            float3(posShadow.xy, RENDERER_SHADOW_MAP_CASCADE_COUNT - 1),
            0
        ) < posShadow.z ? 1.0 : 0.0;
    }

    return shadow;
}

// Approximates Mie scattering.
float PhaseHenyeyGreenstein(float marchDir_dot_lightDir, float scatterFactor)
{
    const float g = scatterFactor;
    const float nom = 1.0 - g * g;
    const float denom = 4.0 * M_PIf * pow(1.0 + g * g - 2.0 * g * marchDir_dot_lightDir, 1.5);
    return nom / denom;
}

[numthreads(RENDERER_FOG_WORKGROUP_SIZE_X, RENDERER_FOG_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 imageSize = uint2(uniformBuffer.renderWidth, uniformBuffer.renderHeight);

    if (dtid.x >= imageSize.x || dtid.y >= imageSize.y)
    {
        return;
    }

    const float2 uv = (dtid.xy + 0.5) * uniformBuffer.renderImageSizeInv;

    const float3 rayStart = uniformBuffer.cameraPosition;
    const float3 rayEnd = CalcPosWorld(uv);
    const float3 rayDir = normalize(rayEnd - rayStart);
    const float3 rayStep = (rayEnd - rayStart) / RAYMARCH_STEPS;

    // We'll dither rays to hide banding artifacts due to low number of raymarching steps.
    float3 samplePosWorld = rayStart + InterleavedGradientNoise(dtid.x, dtid.y) * rayStep;
    float result = 0.0;

    const float3 sunDirWorld = uniformBuffer.sunDirectionWorld;

    for (int i = 0; i < RAYMARCH_STEPS; ++i)
    {
        const float shadow = CalcShadow(samplePosWorld);

        result += PhaseHenyeyGreenstein(dot(rayDir, sunDirWorld), 0.3) * shadow * FOG_INTENSITY;

        samplePosWorld += rayStep;
    }

    result /= RAYMARCH_STEPS;

    outImage[dtid.xy] = result;
}
