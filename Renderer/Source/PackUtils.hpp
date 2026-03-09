#pragma once

#include "Common.hpp"
#include "Math/Utils.hpp"
#include "Math/Vec3.hpp"

inline f32 PackToF32(u8 x, u8 y, u8 z)
{
    const u32 packedU32 = (u32(x) << 16) | (u32(y) << 8) | u32(z);
    const f64 packedF64 = f64(packedU32) / (1U << 24);
    return f32(packedF64);
}

inline f32 PackToF32(Vec3 value)
{
    value *= 255.0f;
    const u8 x = u8(value.X());
    const u8 y = u8(value.Y());
    const u8 z = u8(value.Z());
    const u32 packedU32 = (u32(x) << 16) | (u32(y) << 8) | u32(z);
    const f64 packedF64 = f64(packedU32) / (1U << 24);
    return f32(packedF64);
}

inline Vec3 UnpackToVec3(f32 value)
{
    Vec3 result{};
    result.R() = Fract(value);
    result.G() = Fract(value * 256.0f);
    result.B() = Fract(value * 65536.0f);
    return result;
}
