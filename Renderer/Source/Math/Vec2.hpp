#pragma once

#include "MathCommon.hpp"

inline constexpr f32 Vec2::X() const
{
    return val[0];
}

inline constexpr f32 Vec2::Y() const
{
    return val[1];
}

inline constexpr f32& Vec2::X()
{
    return val[0];
}

inline constexpr f32& Vec2::Y()
{
    return val[1];
}

inline constexpr f32 Vec2::operator[](int i) const
{
    DEBUG_ASSERT(i >= 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline constexpr f32& Vec2::operator[](int i)
{
    DEBUG_ASSERT(i >= 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline constexpr Vec2 operator-(Vec2 v)
{
    return {-v.val[0], -v.val[1]};
}

inline constexpr Vec2& operator+=(Vec2& lhs, Vec2 rhs)
{
    lhs.val[0] += rhs.val[0];
    lhs.val[1] += rhs.val[1];
    return lhs;
}

inline constexpr Vec2& operator-=(Vec2& lhs, Vec2 rhs)
{
    lhs.val[0] -= rhs.val[0];
    lhs.val[1] -= rhs.val[1];
    return lhs;
}

inline constexpr Vec2& operator*=(Vec2& lhs, Vec2 rhs)
{
    lhs.val[0] *= rhs.val[0];
    lhs.val[1] *= rhs.val[1];
    return lhs;
}

inline constexpr Vec2& operator*=(Vec2& lhs, f32 rhs)
{
    lhs.val[0] *= rhs;
    lhs.val[1] *= rhs;
    return lhs;
}

inline constexpr Vec2& operator/=(Vec2& lhs, f32 rhs)
{
    lhs.val[0] /= rhs;
    lhs.val[1] /= rhs;
    return lhs;
}

inline constexpr Vec2 operator+(Vec2 lhs, Vec2 rhs)
{
    return {lhs.val[0] + rhs.val[0], lhs.val[1] + rhs.val[1]};
}

inline constexpr Vec2 operator-(Vec2 lhs, Vec2 rhs)
{
    return {lhs.val[0] - rhs.val[0], lhs.val[1] - rhs.val[1]};
}

inline constexpr Vec2 operator*(Vec2 lhs, Vec2 rhs)
{
    return {lhs.val[0] * rhs.val[0], lhs.val[1] * rhs.val[1]};
}

inline constexpr Vec2 operator*(Vec2 lhs, f32 rhs)
{
    return {lhs.val[0] * rhs, lhs.val[1] * rhs};
}

inline constexpr Vec2 operator/(Vec2 lhs, f32 rhs)
{
    return {lhs.val[0] / rhs, lhs.val[1] / rhs};
}

inline constexpr Vec2 operator*(f32 lhs, Vec2 rhs)
{
    return {lhs * rhs.val[0], lhs * rhs.val[1]};
}

[[nodiscard]]
inline constexpr bool operator==(Vec2 lhs, Vec2 rhs)
{
    return (lhs.val[0] == rhs.val[0]) && (lhs.val[1] == rhs.val[1]);
}

[[nodiscard]]
inline constexpr bool operator!=(Vec2 lhs, Vec2 rhs)
{
    return (lhs.val[0] != rhs.val[0]) || (lhs.val[1] != rhs.val[1]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Vec2 lhs, Vec2 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.val[0], rhs.val[0], tolerance)
        && AlmostEqual(lhs.val[1], rhs.val[1], tolerance);
}

inline constexpr Vec2 Abs(Vec2 v)
{
    return {fabsf(v.val[0]), fabsf(v.val[1])};
}

[[nodiscard]]
inline constexpr f32 Dot(Vec2 a, Vec2 b)
{
    return a.val[0] * b.val[0] + a.val[1] * b.val[1];
}

[[nodiscard]]
inline constexpr f32 MagnitudeSq(Vec2 v)
{
    return v.val[0] * v.val[0] + v.val[1] * v.val[1];
}

[[nodiscard]]
inline constexpr f32 Magnitude(Vec2 v)
{
    return sqrtf(v.val[0] * v.val[0] + v.val[1] * v.val[1]);
}

inline constexpr Vec2 Normalize(Vec2 v)
{
    const f32 mag = sqrtf(v.val[0] * v.val[0] + v.val[1] * v.val[1]);
    DEBUG_ASSERT(mag != 0.0f);
    return {v.val[0] / mag, v.val[1] / mag};
}

inline constexpr Vec2 PerpCW(Vec2 a)
{
    return {a.val[1], -a.val[0]};
}

inline constexpr Vec2 PerpCWScale(Vec2 a, f32 s)
{
    return {s * a.val[1], -s * a.val[0]};
}

inline constexpr Vec2 PerpCCW(Vec2 a)
{
    return {-a.val[1], a.val[0]};
}

inline constexpr Vec2 PerpCCWScale(Vec2 a, f32 s)
{
    return {-s * a.val[1], s * a.val[0]};
}

[[nodiscard]]
inline constexpr f32 PerpDot(Vec2 a, Vec2 b)
{
    return a.val[0] * b.val[1] - a.val[1] * b.val[0];
}

inline constexpr void Clear(Vec2& v)
{
    v[0] = 0.0f;
    v[1] = 0.0f;
}
