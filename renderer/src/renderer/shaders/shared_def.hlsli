#pragma once

// Shared with C++.

#include "shared_config.hlsli"

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

struct UniformData {
    MAT_4X4 world_to_view;
    MAT_4X4 cull_world_to_view;
    MAT_4X4 world_to_clip;
    MAT_4X4 view_to_clip;
    MAT_4X4 prev_world_to_clip;
    MAT_4X4 clip_to_world;
    FLOAT_3 camera_position;
    FLOAT_3 sun_direction_world;
    FLOAT_3 sun_color;
    FLOAT_2 taa_jitter;
    FLOAT_2 prev_taa_jitter;
    FLOAT_2 view_to_clip_inv0011;
    FLOAT_2 render_texture_size_inv;
    float delta_time;
    float sun_intensity;
    float ambient_intensity;
    float grad_error_max;
    float aspect;
    uint64_t frame_count;
    uint32_t swapchain_width;
    uint32_t swapchain_height;
    uint32_t render_width;
    uint32_t render_height;
    uint32_t draw_count;
    uint32_t render_mode;
    uint32_t enable_ssao;
    uint32_t enable_fog;
};

struct ShadowPassData {
    // TODO: 1 matrix plus offsets and scales for each cascade should be enough.
    MAT_4X4 world_to_clip[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    float texel_sizes[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    // float frustum_plane_xx[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    // float frustum_plane_xz[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    // float frustum_plane_yy[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    // float frustum_plane_yz[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    float pcf_kernel_cascade_scales[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    float normal_offset;
    float constant_offset;
    float pcf_kernel_scale;
    uint32_t enable_cascade_color;
    uint32_t enable_pcf;
};

struct PushConstantsCull {
    float frustum_plane_xx;
    float frustum_plane_xz;
    float frustum_plane_yy;
    float frustum_plane_yz;
    float depth_pyramid_width;
    float depth_pyramid_height;
    uint32_t draw_cull_aabb;
};

struct PushConstantsSSAO {
    FLOAT_2 texture_size_inv;
    uint32_t width;
    uint32_t height;
    int32_t pixel_offset_x;
    int32_t pixel_offset_y;
};

struct PushConstantsTAA {
    float blend_weight;
    uint32_t enable;
};

struct PushConstantsImgui {
    FLOAT_2 scale;
    FLOAT_2 translate;
};

struct PushConstantsVisibilityBuffer {
    uint32_t cull_late;
};

struct PushConstantsDepthReduce {
    uint32_t mip_level;
    uint32_t out_width;
    uint32_t out_height;
};

struct PushConstantsShadow {
    int32_t shadow_cascade_idx;
    uint32_t render_pass_flags;
};

struct PushConstantsFogBlur {
    uint32_t horizontal;
};

struct Vertex {
    HALF px, py, pz;
    uint16_t normal; // 8-8 octahedral.
    uint32_t tangent; // 8-8 octahedral in bits 0-15, bit 31 bitangent sign, 15 free bits.
    HALF u, v;
};

struct Material {
    uint32_t albedo_tex_idx;
    uint32_t metallic_roughness_tex_idx;
    uint32_t normal_tex_idx;

    FLOAT_4 albedo_factor;
    float metallic_factor;
    float roughness_factor;
};

struct DrawData {
    FLOAT_3 position;
    float scale;
    QUAT orientation;

    // TODO: separate buffer.
    FLOAT_3 sphere_center;
    float sphere_radius;

    uint32_t material_idx;
    uint32_t render_pass_flags;
};

struct DebugDrawRectData {
    FLOAT_4 lbrt_screen;
    uint32_t color;
};

enum RenderPassFlagBits {
    RENDER_PASS_OPAQUE_BIT      = (1U << 0),
    RENDER_PASS_TRANSLUCENT_BIT = (1U << 1),
};

enum RenderMode {
    RENDER_MODE_VISIBILITY,
    RENDER_MODE_GRAD_ERROR,
    RENDER_MODE_AMBIENT_OCCLUSION,
    RENDER_MODE_COUNT,
};
