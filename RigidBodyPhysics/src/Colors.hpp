#pragma once

#include "Common.hpp"
#include "Math/Vec3.hpp"

struct Color
{
    u8 mR;
    u8 mG;
    u8 mB;
};

namespace Colors
{

// Approximation.
inline f32 LinearToSrgb(f32 color, f32 gamma = 2.2f)
{
    assert(gamma > 0.0f);
    return powf(color, 1.0f / gamma);
}

// Approximation.
inline f32 SrgbToLinear(f32 color, f32 gamma = 2.2f)
{
    assert(gamma > 0.0f);
    return powf(color, gamma);
}

// Approximation.
inline Vec3 LinearToSrgb(Vec3 color, f32 gamma = 2.2f)
{
    assert(gamma > 0.0f);
    f32 inverseGamma = 1.0f / gamma;
    return {
        powf(color.R(), inverseGamma),
        powf(color.G(), inverseGamma),
        powf(color.B(), inverseGamma)
    };
}

// Approximation.
inline Vec3 LinearToSrgb(Color color, f32 gamma = 2.2f)
{
    assert(gamma > 0.0f);
    return LinearToSrgb(Vec3{color.mR / 255.0f, color.mG / 255.0f, color.mB / 255.0f}, gamma);
}

// Approximation.
inline Vec3 SrgbToLinear(Vec3 color, f32 gamma = 2.2f)
{
    assert(gamma > 0.0f);
    return {powf(color.R(), gamma), powf(color.G(), gamma), powf(color.B(), gamma)};
}

// Approximation.
inline Vec3 SrgbToLinear(Color color, f32 gamma = 2.2f)
{
    assert(gamma > 0.0f);
    return SrgbToLinear(Vec3{color.mR / 255.0f, color.mG / 255.0f, color.mB / 255.0f}, gamma);
}

}
