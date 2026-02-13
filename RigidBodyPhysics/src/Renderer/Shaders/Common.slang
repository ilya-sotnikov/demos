#pragma once

#include "SharedConstants.slang"

static const float PI = 3.14159265358979323846;

struct UniformData
{
    float4x4 worldToClip;
    float4x4 worldToView;
    float4x4 viewToClip;
    float3 cameraPosition;
    float3 lightDirectionWorld;
    float3 lightDirectionView;
    float3 lightColor;
    uint enableShadowCascadesColor;
    uint enableShadowPcf;
    uint enableShadowTexelColoring;
    uint perspectiveChosen;

    struct
    {
        float texelSizes[RENDERER_SHADOW_MAP_CASCADE_COUNT];
        float4x4 worldToClip[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    } shadow;
};
[[vk::binding(0)]]
ConstantBuffer<UniformData> uniformData;

float3 UnpackFloatToFloat3(float color)
{
    float3 result;
    result.r = fract(color);
    result.g = fract(color * 256.0);
    result.b = fract(color * 65536.0);
    return result;
}
