#pragma once

// Shared with C++.

#include "SharedConfig.hlsli"

#ifdef __cplusplus
#define HALF f16
#define HALF_2 Half2
#define HALF_3 Half3
#define HALF_4 Half4
#define FLOAT_2 Vec2
#define FLOAT_3 Vec3
#define FLOAT_4 Vec4
#define QUAT Quat
#define MAT_4X4 Mat4
#else
#define uint32_t uint
#define int32_t int
#define HALF half
#define HALF_2 half2
#define HALF_3 half3
#define HALF_4 half4
#define FLOAT_2 float2
#define FLOAT_3 float3
#define FLOAT_4 float4
#define QUAT float4
#define MAT_4X4 float4x4
#endif

#ifdef __cplusplus
#define GPU_PTR(type) VkDeviceAddress
#define GPU_CONST_PTR(type) VkDeviceAddress
#else
#define GPU_PTR(type) Ptr<type, Access.ReadWrite>
#define GPU_CONST_PTR(type) Ptr<type, Access.Read>
#endif

struct UniformData
{
    MAT_4X4 worldToView;
    MAT_4X4 cullWorldToView;
    MAT_4X4 worldToClip;
    MAT_4X4 viewToClip;
    MAT_4X4 prevWorldToClip;
    MAT_4X4 clipToWorld;
    FLOAT_3 cameraPosition;
    FLOAT_3 sunDirectionWorld;
    FLOAT_3 sunDirectionView;
    FLOAT_2 taaJitter;
    FLOAT_2 prevTaaJitter;
    float taaBlendWeight;
    float cullFrustumPlaneXX;
    float cullFrustumPlaneXZ;
    float cullFrustumPlaneYY;
    float cullFrustumPlaneYZ;
    float deltaTime;
    float sunIntensity;
    float ambientIntensity;
    float gradErrorMax;
    float depthPyramidWidth;
    float depthPyramidHeight;
    uint64_t frameCount;
    uint32_t swapchainWidth;
    uint32_t swapchainHeight;
    uint32_t renderWidth;
    uint32_t renderHeight;
    uint32_t drawCount;
    uint32_t taaEnable;
    uint32_t drawCullAABB;
};

struct Vertex
{
    HALF px, py, pz;
    HALF nx, ny; // 16 bit octahedral.
    HALF u, v;
};

struct Material
{
    uint32_t albedoTexIdx;
    uint32_t metallicRoughnessTexIdx;
    uint32_t normalTexIdx;

    FLOAT_4 albedoFactor;
    float metallicFactor;
    float roughnessFactor;
};

struct DrawData
{
    FLOAT_3 position;
    float scale;
    QUAT orientation;

    // TODO: separate buffer.
    FLOAT_3 sphereCenter;
    float sphereRadius;

    uint32_t materialIdx;
    uint32_t renderPassFlags;
};

struct DebugDrawRectData
{
    FLOAT_4 lbrtScreen;
    uint32_t color;
};

struct PushConstantsImgui
{
    FLOAT_2 scale;
    FLOAT_2 translate;
};

struct PushConstantsVisibilityBuffer
{
    uint32_t cullLate;
};

struct PushConstantsDepthReduce
{
    uint32_t mipLevel;
    uint32_t outWidth;
    uint32_t outHeight;
};

enum RenderPassFlagBits
{
    RENDER_PASS_OPAQUE_BIT      = (1U << 0),
    RENDER_PASS_TRANSLUCENT_BIT = (1U << 1),
};
