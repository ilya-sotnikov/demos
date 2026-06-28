#pragma once

#include "Common.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#define GLM_FORCE_LEFT_HANDED

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUG__)
#pragma GCC diagnostic pop
#endif

inline glm::mat4 Perspective(f32 fovYRad, f32 aspectWbyH, float zNear)
{
    const f32 f = 1.0f / tanf(fovYRad / 2.0f);
    // clang-format off
    return glm::mat4(
        f / aspectWbyH, 0.0f, 0.0f, 0.0f,
        0.0f, f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, zNear, 0.0f
    );
    // clang-format on
}
