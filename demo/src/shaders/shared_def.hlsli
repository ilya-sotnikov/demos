#pragma once

// Shared with C++.

#ifdef __cplusplus
#define FLOAT_2 glm::vec2
#define FLOAT_3 glm::vec3
#define FLOAT_4 glm::vec4
#define MAT_4X4 glm::mat4
#else
#define uint32_t uint
#define int32_t int
#define FLOAT_2 float2
#define FLOAT_3 float3
#define FLOAT_4 float4
#define MAT_4X4 float4x4
#endif

struct UniformData
{
    MAT_4X4 worldToClip;
    FLOAT_3 cameraPositionWorld;
};
