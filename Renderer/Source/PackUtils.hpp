#pragma once

#include "Common.hpp"
#include "Math/Utils.hpp"
#include "Math/Vec2.hpp"
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

// https://www.elopezr.com/the-art-of-packing-data/
inline Vec2 PackNormalOctahedral(Vec3 normal)
{
    normal /= fabsf(normal.X()) + fabsf(normal.Y()) + fabsf(normal.Z());
    if (normal.Z() < 0.0f)
    {
        const f32 x = normal.X();
        const f32 y = normal.Y();
        normal.X() = (1.0f - fabsf(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        normal.Y() = (1.0f - fabsf(x)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    normal.X() = normal.X() * 0.5f + 0.5f;
    normal.Y() = normal.Y() * 0.5f + 0.5f;
    return {normal.X(), normal.Y()};
}

inline Vec3 UnpackNormalOctahedral(Vec2 packed)
{
    packed = packed * 2.0f - Vec2{1.0f};

    Vec3 n = {packed.X(), packed.Y(), 1.0f - fabsf(packed.X()) - fabsf(packed.Y())};
    const float t = Clamp(-n.Z(), 0.0f, 1.0f);
    n.X() += n.X() >= 0.0f ? -t : t;
    n.Y() += n.Y() >= 0.0f ? -t : t;
    return Normalize(n);
}
