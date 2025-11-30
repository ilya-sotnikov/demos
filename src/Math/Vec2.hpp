#pragma once

#include "MathCommon.hpp"

inline constexpr f32 Vec2::X() const
{
    return mVal[0];
}

inline constexpr f32 Vec2::Y() const
{
    return mVal[1];
}

inline constexpr f32& Vec2::X()
{
    return mVal[0];
}

inline constexpr f32& Vec2::Y()
{
    return mVal[1];
}

inline constexpr f32 Vec2::operator[](int i) const
{
    assert(i >= 0);
    assert(i < N);
    return mVal[i];
}

inline constexpr f32& Vec2::operator[](int i)
{
    assert(i >= 0);
    assert(i < N);
    return mVal[i];
}

inline constexpr Vec2 operator-(Vec2 v)
{
    return {-v.mVal[0], -v.mVal[1]};
}

inline constexpr Vec2& operator+=(Vec2& lhs, Vec2 rhs)
{
    lhs.mVal[0] += rhs.mVal[0];
    lhs.mVal[1] += rhs.mVal[1];
    return lhs;
}

inline constexpr Vec2& operator-=(Vec2& lhs, Vec2 rhs)
{
    lhs.mVal[0] -= rhs.mVal[0];
    lhs.mVal[1] -= rhs.mVal[1];
    return lhs;
}

inline constexpr Vec2& operator*=(Vec2& lhs, Vec2 rhs)
{
    lhs.mVal[0] *= rhs.mVal[0];
    lhs.mVal[1] *= rhs.mVal[1];
    return lhs;
}

inline constexpr Vec2& operator*=(Vec2& lhs, f32 rhs)
{
    lhs.mVal[0] *= rhs;
    lhs.mVal[1] *= rhs;
    return lhs;
}

inline constexpr Vec2& operator/=(Vec2& lhs, f32 rhs)
{
    lhs.mVal[0] /= rhs;
    lhs.mVal[1] /= rhs;
    return lhs;
}

inline constexpr Vec2 operator+(Vec2 lhs, Vec2 rhs)
{
    return {lhs.mVal[0] + rhs.mVal[0], lhs.mVal[1] + rhs.mVal[1]};
}

inline constexpr Vec2 operator-(Vec2 lhs, Vec2 rhs)
{
    return {lhs.mVal[0] - rhs.mVal[0], lhs.mVal[1] - rhs.mVal[1]};
}

inline constexpr Vec2 operator*(Vec2 lhs, Vec2 rhs)
{
    return {lhs.mVal[0] * rhs.mVal[0], lhs.mVal[1] * rhs.mVal[1]};
}

inline constexpr Vec2 operator*(Vec2 lhs, f32 rhs)
{
    return {lhs.mVal[0] * rhs, lhs.mVal[1] * rhs};
}

inline constexpr Vec2 operator/(Vec2 lhs, f32 rhs)
{
    return {lhs.mVal[0] / rhs, lhs.mVal[1] / rhs};
}

inline constexpr Vec2 operator*(f32 lhs, Vec2 rhs)
{
    return {lhs * rhs.mVal[0], lhs * rhs.mVal[1]};
}

[[nodiscard]]
inline constexpr bool operator==(Vec2 lhs, Vec2 rhs)
{
    return (lhs.mVal[0] == rhs.mVal[0]) && (lhs.mVal[1] == rhs.mVal[1]);
}

[[nodiscard]]
inline constexpr bool operator!=(Vec2 lhs, Vec2 rhs)
{
    return (lhs.mVal[0] != rhs.mVal[0]) || (lhs.mVal[1] != rhs.mVal[1]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Vec2 lhs, Vec2 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.mVal[0], rhs.mVal[0], tolerance)
        && AlmostEqual(lhs.mVal[1], rhs.mVal[1], tolerance);
}

inline constexpr Vec2 Abs(Vec2 v)
{
    return {fabsf(v.mVal[0]), fabsf(v.mVal[1])};
}

[[nodiscard]]
inline constexpr f32 Dot(Vec2 a, Vec2 b)
{
    return a.mVal[0] * b.mVal[0] + a.mVal[1] * b.mVal[1];
}

[[nodiscard]]
inline constexpr f32 MagnitudeSq(Vec2 v)
{
    return v.mVal[0] * v.mVal[0] + v.mVal[1] * v.mVal[1];
}

[[nodiscard]]
inline constexpr f32 Magnitude(Vec2 v)
{
    return sqrtf(v.mVal[0] * v.mVal[0] + v.mVal[1] * v.mVal[1]);
}

inline constexpr Vec2 Normalize(Vec2 v)
{
    const f32 mag = sqrtf(v.mVal[0] * v.mVal[0] + v.mVal[1] * v.mVal[1]);
    assert(mag != 0.0f);
    return {v.mVal[0] / mag, v.mVal[1] / mag};
}

inline constexpr Vec2 PerpCW(Vec2 a)
{
    return {a.mVal[1], -a.mVal[0]};
}

inline constexpr Vec2 PerpCWScale(Vec2 a, f32 s)
{
    return {s * a.mVal[1], -s * a.mVal[0]};
}

inline constexpr Vec2 PerpCCW(Vec2 a)
{
    return {-a.mVal[1], a.mVal[0]};
}

inline constexpr Vec2 PerpCCWScale(Vec2 a, f32 s)
{
    return {-s * a.mVal[1], s * a.mVal[0]};
}

[[nodiscard]]
inline constexpr f32 PerpDot(Vec2 a, Vec2 b)
{
    return a.mVal[0] * b.mVal[1] - a.mVal[1] * b.mVal[0];
}

inline constexpr void Clear(Vec2& v)
{
    v[0] = 0.0f;
    v[1] = 0.0f;
}
