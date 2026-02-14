#pragma once

// Shared with C++.

#ifdef __cplusplus
#define HALF f16
#define HALF_2 Half2
#define HALF_3 Half3
#define HALF_4 Half4
#define FLOAT_2 Vec2
#define FLOAT_3 Vec3
#define FLOAT_4 Vec4
#define MAT_4X4 Mat4
#else
#define HALF half
#define HALF_2 half2
#define HALF_3 half3
#define HALF_4 half4
#define FLOAT_2 float2
#define FLOAT_3 float3
#define FLOAT_4 float4
#define MAT_4X4 float4x4
#endif

struct Emitter
{
    FLOAT_3 position;
    float life;
    float lifeInv;
    float positionInterval; // Offsets [-pos, pos].
    FLOAT_3 color;
    float radiusMin;
    float radius; // [min, min + r].
    float massInv;
    FLOAT_3 initialVelocity;
    float initialVelocityInterval;
};

struct SphereForceField
{
    FLOAT_3 position;
    float radius;
    float constForce;
};

struct UniformData
{
    MAT_4X4 worldToView;
    MAT_4X4 worldToClip;
    MAT_4X4 viewToClip;
    uint64_t frameCount;
    uint32_t swapchainWidth;
    uint32_t swapchainHeight;
    float deltaTime;

    uint32_t emitterCount;
    float emitterPeriod;
    float emitterTimeAccum;
    uint32_t emitterIterations;
    float particleSimTimeAccum;
    uint32_t particleSimIterations;

    SphereForceField forceField;
};

struct ParticleCountData
{
    uint32_t deadCount;
    uint32_t aliveCount1;
    uint32_t aliveCount2;
};
