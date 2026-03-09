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
    MAT_4X4 worldToClip;
    MAT_4X4 viewToClip;
    MAT_4X4 clipToWorld;
    FLOAT_3 cameraPosition;
    FLOAT_3 sunDirectionWorld;
    FLOAT_3 sunDirectionView;
    float frustumPlaneXX;
    float frustumPlaneXZ;
    float frustumPlaneYY;
    float frustumPlaneYZ;
    float deltaTime;
    float exposure;
    uint64_t frameCount;
    uint32_t swapchainWidth;
    uint32_t swapchainHeight;
    uint32_t drawCount;

};

// TODO: pack.
struct Vertex
{
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
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

// TODO: separate quat, single scale, translation components.
struct DrawData
{
    MAT_4X4 localToWorld;
    FLOAT_3 sphereCenter;
    float scale;
    float sphereRadius;
    uint32_t materialIdx;
    uint32_t renderPassFlags;
};

struct DrawCmd
{
    uint32_t idx;
    VkDrawIndexedIndirectCommand cmd;
};

struct PushConstants
{
    GPU_CONST_PTR(UniformData) uniformData;
    GPU_CONST_PTR(Vertex) vertices;
    GPU_CONST_PTR(Material) materials;
    GPU_CONST_PTR(DrawData) drawData;
    GPU_PTR(uint32_t) indirectCountPtr;
    GPU_CONST_PTR(VkDrawIndexedIndirectCommand) drawCmds1;
    GPU_PTR(DrawCmd) drawCmds2;
    uint64_t tlas; // TODO: seems to be the only way in slang for now?

    uint32_t renderPassFlags;
};

enum {
    COMBINED_IMAGE_SAMPLER_BINDING,
    STORAGE_IMAGE_BINDING,
    STORAGE_IMAGE_RW_BINDING,
    SAMPLER_BINDING,
    SAMPLED_IMAGE_BINDING,
    DESCRIPTOR_BINDING_COUNT,
};

enum {
    COMBINED_IMAGE_SAMPLER_RENDER_IDX,
    COMBINED_IMAGE_SAMPLER_ALBEDO_IDX,
    COMBINED_IMAGE_SAMPLER_SHADOW_IDX,
    COMBINED_IMAGE_SAMPLER_DEPTH_IDX,
    COMBINED_IMAGE_SAMPLER_COUNT,
};

enum {
    STORAGE_IMAGE_RW_SHADOW_IDX,
    STORAGE_IMAGE_RW_COUNT,
};

enum {
    SAMPLER_TEXTURE_IDX,
    SAMPLER_COUNT,
};

enum RenderPassFlagBits
{
    RENDER_PASS_OPAQUE_BIT  = (1U << 0),
    RENDER_PASS_TRANSLUCENT = (1U << 1),
};
