#pragma once

#include "MathCommon.hpp"

inline f32 Vec2::X() const
{
    return val[0];
}

inline f32 Vec2::Y() const
{
    return val[1];
}

inline f32& Vec2::X()
{
    return val[0];
}

inline f32& Vec2::Y()
{
    return val[1];
}

inline f32 Vec2::operator[](int i) const
{
    DEBUG_ASSERT(i >= 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline f32& Vec2::operator[](int i)
{
    DEBUG_ASSERT(i >= 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline Vec2 operator-(Vec2 v)
{
    return {-v.val[0], -v.val[1]};
}

inline Vec2& operator+=(Vec2& lhs, Vec2 rhs)
{
    lhs.val[0] += rhs.val[0];
    lhs.val[1] += rhs.val[1];
    return lhs;
}

inline Vec2& operator-=(Vec2& lhs, Vec2 rhs)
{
    lhs.val[0] -= rhs.val[0];
    lhs.val[1] -= rhs.val[1];
    return lhs;
}

inline Vec2& operator*=(Vec2& lhs, Vec2 rhs)
{
    lhs.val[0] *= rhs.val[0];
    lhs.val[1] *= rhs.val[1];
    return lhs;
}

inline Vec2& operator*=(Vec2& lhs, f32 rhs)
{
    lhs.val[0] *= rhs;
    lhs.val[1] *= rhs;
    return lhs;
}

inline Vec2& operator/=(Vec2& lhs, f32 rhs)
{
    lhs.val[0] /= rhs;
    lhs.val[1] /= rhs;
    return lhs;
}

inline Vec2& operator/=(Vec2& lhs, Vec2 rhs)
{
    lhs.val[0] /= rhs.val[0];
    lhs.val[1] /= rhs.val[1];
    return lhs;
}

inline Vec2 operator+(Vec2 lhs, Vec2 rhs)
{
    return {lhs.val[0] + rhs.val[0], lhs.val[1] + rhs.val[1]};
}

inline Vec2 operator-(Vec2 lhs, Vec2 rhs)
{
    return {lhs.val[0] - rhs.val[0], lhs.val[1] - rhs.val[1]};
}

inline Vec2 operator*(Vec2 lhs, Vec2 rhs)
{
    return {lhs.val[0] * rhs.val[0], lhs.val[1] * rhs.val[1]};
}

inline Vec2 operator*(Vec2 lhs, f32 rhs)
{
    return {lhs.val[0] * rhs, lhs.val[1] * rhs};
}

inline Vec2 operator/(Vec2 lhs, f32 rhs)
{
    return {lhs.val[0] / rhs, lhs.val[1] / rhs};
}

inline Vec2 operator/(Vec2 lhs, Vec2 rhs)
{
    return {lhs.val[0] / rhs.val[0], lhs.val[1] / rhs.val[1]};
}

inline Vec2 operator*(f32 lhs, Vec2 rhs)
{
    return {lhs * rhs.val[0], lhs * rhs.val[1]};
}

inline bool operator==(Vec2 lhs, Vec2 rhs)
{
    return (lhs.val[0] == rhs.val[0]) && (lhs.val[1] == rhs.val[1]);
}

inline bool operator!=(Vec2 lhs, Vec2 rhs)
{
    return (lhs.val[0] != rhs.val[0]) || (lhs.val[1] != rhs.val[1]);
}

inline bool AlmostEqual(Vec2 lhs, Vec2 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.val[0], rhs.val[0], tolerance)
        && AlmostEqual(lhs.val[1], rhs.val[1], tolerance);
}

inline Vec2 Abs(Vec2 v)
{
    return {fabsf(v.val[0]), fabsf(v.val[1])};
}

inline f32 Dot(Vec2 a, Vec2 b)
{
    return a.val[0] * b.val[0] + a.val[1] * b.val[1];
}

inline f32 MagnitudeSq(Vec2 v)
{
    return v.val[0] * v.val[0] + v.val[1] * v.val[1];
}

inline f32 Magnitude(Vec2 v)
{
    return sqrtf(v.val[0] * v.val[0] + v.val[1] * v.val[1]);
}

inline Vec2 Normalize(Vec2 v)
{
    const f32 mag = sqrtf(v.val[0] * v.val[0] + v.val[1] * v.val[1]);
    DEBUG_ASSERT(mag != 0.0f);
    return {v.val[0] / mag, v.val[1] / mag};
}

inline Vec2 PerpCW(Vec2 a)
{
    return {a.val[1], -a.val[0]};
}

inline Vec2 PerpCWScale(Vec2 a, f32 s)
{
    return {s * a.val[1], -s * a.val[0]};
}

inline Vec2 PerpCCW(Vec2 a)
{
    return {-a.val[1], a.val[0]};
}

inline Vec2 PerpCCWScale(Vec2 a, f32 s)
{
    return {-s * a.val[1], s * a.val[0]};
}

inline f32 PerpDot(Vec2 a, Vec2 b)
{
    return a.val[0] * b.val[1] - a.val[1] * b.val[0];
}

inline void Clear(Vec2& v)
{
    v[0] = 0.0f;
    v[1] = 0.0f;
}
