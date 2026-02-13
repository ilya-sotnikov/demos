#pragma once

#include "MathCommon.hpp"

inline constexpr f32 Vec3::X() const
{
    return mVal[0];
}

inline constexpr f32 Vec3::Y() const
{
    return mVal[1];
}

inline constexpr f32 Vec3::Z() const
{
    return mVal[2];
}

inline constexpr f32 Vec3::R() const
{
    return mVal[0];
}

inline constexpr f32 Vec3::G() const
{
    return mVal[1];
}

inline constexpr f32 Vec3::B() const
{
    return mVal[2];
}

inline constexpr f32& Vec3::X()
{
    return mVal[0];
}

inline constexpr f32& Vec3::Y()
{
    return mVal[1];
}

inline constexpr f32& Vec3::Z()
{
    return mVal[2];
}

inline constexpr f32& Vec3::R()
{
    return mVal[0];
}

inline constexpr f32& Vec3::G()
{
    return mVal[1];
}

inline constexpr f32& Vec3::B()
{
    return mVal[2];
}

inline constexpr f32 Vec3::operator[](int i) const
{
    assert(i >= 0);
    assert(i < N);
    return mVal[i];
}

inline constexpr f32& Vec3::operator[](int i)
{
    assert(i >= 0);
    assert(i < N);
    return mVal[i];
}

inline constexpr Vec3 operator-(Vec3 v)
{
    return {-v.mVal[0], -v.mVal[1], -v.mVal[2]};
}

inline constexpr Vec3& operator+=(Vec3& lhs, Vec3 rhs)
{
    lhs.mVal[0] += rhs.mVal[0];
    lhs.mVal[1] += rhs.mVal[1];
    lhs.mVal[2] += rhs.mVal[2];
    return lhs;
}

inline constexpr Vec3& operator-=(Vec3& lhs, Vec3 rhs)
{
    lhs.mVal[0] -= rhs.mVal[0];
    lhs.mVal[1] -= rhs.mVal[1];
    lhs.mVal[2] -= rhs.mVal[2];
    return lhs;
}

inline constexpr Vec3& operator*=(Vec3& lhs, Vec3 rhs)
{
    lhs.mVal[0] *= rhs.mVal[0];
    lhs.mVal[1] *= rhs.mVal[1];
    lhs.mVal[2] *= rhs.mVal[2];
    return lhs;
}

inline constexpr Vec3& operator*=(Vec3& lhs, f32 rhs)
{
    lhs.mVal[0] *= rhs;
    lhs.mVal[1] *= rhs;
    lhs.mVal[2] *= rhs;
    return lhs;
}

inline constexpr Vec3& operator/=(Vec3& lhs, f32 rhs)
{
    lhs.mVal[0] /= rhs;
    lhs.mVal[1] /= rhs;
    lhs.mVal[2] /= rhs;
    return lhs;
}

inline constexpr Vec3 operator+(Vec3 lhs, Vec3 rhs)
{
    return {lhs.mVal[0] + rhs.mVal[0], lhs.mVal[1] + rhs.mVal[1], lhs.mVal[2] + rhs.mVal[2]};
}

inline constexpr Vec3 operator-(Vec3 lhs, Vec3 rhs)
{
    return {lhs.mVal[0] - rhs.mVal[0], lhs.mVal[1] - rhs.mVal[1], lhs.mVal[2] - rhs.mVal[2]};
}

inline constexpr Vec3 operator*(Vec3 lhs, Vec3 rhs)
{
    return {lhs.mVal[0] * rhs.mVal[0], lhs.mVal[1] * rhs.mVal[1], lhs.mVal[2] * rhs.mVal[2]};
}

inline constexpr Vec3 operator*(Vec3 lhs, f32 rhs)
{
    return {lhs.mVal[0] * rhs, lhs.mVal[1] * rhs, lhs.mVal[2] * rhs};
}

inline constexpr Vec3 operator/(Vec3 lhs, f32 rhs)
{
    return {lhs.mVal[0] / rhs, lhs.mVal[1] / rhs, lhs.mVal[2] / rhs};
}

inline constexpr Vec3 operator*(f32 lhs, Vec3 rhs)
{
    return {lhs * rhs.mVal[0], lhs * rhs.mVal[1], lhs * rhs.mVal[2]};
}

[[nodiscard]]
inline constexpr bool operator==(Vec3 lhs, Vec3 rhs)
{
    return (lhs.mVal[0] == rhs.mVal[0]) && (lhs.mVal[1] == rhs.mVal[1])
        && (lhs.mVal[2] == rhs.mVal[2]);
}

[[nodiscard]]
inline constexpr bool operator!=(Vec3 lhs, Vec3 rhs)
{
    return (lhs.mVal[0] != rhs.mVal[0]) || (lhs.mVal[1] != rhs.mVal[1])
        || (lhs.mVal[2] != rhs.mVal[2]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Vec3 lhs, Vec3 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.mVal[0], rhs.mVal[0], tolerance)
        && AlmostEqual(lhs.mVal[1], rhs.mVal[1], tolerance)
        && AlmostEqual(lhs.mVal[2], rhs.mVal[2], tolerance);
}

inline constexpr Vec3 Abs(Vec3 v)
{
    return {fabsf(v.mVal[0]), fabsf(v.mVal[1]), fabsf(v.mVal[2])};
}

[[nodiscard]]
inline constexpr f32 Dot(Vec3 a, Vec3 b)
{
    return a.mVal[0] * b.mVal[0] + a.mVal[1] * b.mVal[1] + a.mVal[2] * b.mVal[2];
}

[[nodiscard]]
inline constexpr f32 MagnitudeSq(Vec3 v)
{
    return v.mVal[0] * v.mVal[0] + v.mVal[1] * v.mVal[1] + v.mVal[2] * v.mVal[2];
}

[[nodiscard]]
inline constexpr f32 Magnitude(Vec3 v)
{
    return sqrtf(v.mVal[0] * v.mVal[0] + v.mVal[1] * v.mVal[1] + v.mVal[2] * v.mVal[2]);
}

inline constexpr Vec3 Normalize(Vec3 v)
{
    const f32 mag = sqrtf(v.mVal[0] * v.mVal[0] + v.mVal[1] * v.mVal[1] + v.mVal[2] * v.mVal[2]);
    assert(mag != 0.0f);
    return {v.mVal[0] / mag, v.mVal[1] / mag, v.mVal[2] / mag};
}

inline constexpr Vec3 Cross(Vec3 a, Vec3 b)
{
    const f32 x = a.mVal[1] * b.mVal[2] - b.mVal[1] * a.mVal[2];
    const f32 y = a.mVal[2] * b.mVal[0] - a.mVal[0] * b.mVal[2];
    const f32 z = a.mVal[0] * b.mVal[1] - b.mVal[0] * a.mVal[1];
    return {x, y, z};
}

inline constexpr f32 TripleProduct(Vec3 a, Vec3 b, Vec3 c)
{
    const f32 x = a.mVal[1] * b.mVal[2] - b.mVal[1] * a.mVal[2];
    const f32 y = a.mVal[2] * b.mVal[0] - a.mVal[0] * b.mVal[2];
    const f32 z = a.mVal[0] * b.mVal[1] - b.mVal[0] * a.mVal[1];
    return x * c.mVal[0] + y * c.mVal[1] + z * c.mVal[2];
}

inline constexpr Vec3 Lerp(Vec3 a, Vec3 b, f32 t)
{
    const f32 x = 1.0f - t;
    return {
        a.mVal[0] * t + b.mVal[0] * x,
        a.mVal[1] * t + b.mVal[1] * x,
        a.mVal[2] * t + b.mVal[2] * x,
    };
}

// https://box2d.org/posts/2014/02/computing-a-basis/
inline constexpr void ComputeBasis(Vec3 normal, Vec3& tangent1, Vec3& tangent2)
{
    // Suppose vector a has all equal components and is a unit vector:
    // a = (s, s, s)
    // Then 3*s*s = 1, s = sqrt(1/3) = 0.57735. This means that at
    // least one component of a unit vector must be greater or equal
    // to 0.57735.
    if (fabsf(normal.mVal[0]) >= 0.57735f)
    {
        tangent1 = {normal.mVal[1], -normal.mVal[0], 0.0f};
    }
    else
    {
        tangent1 = {0.0f, normal.mVal[2], -normal.mVal[1]};
    }

    tangent1 = Normalize(tangent1);
    tangent2 = Cross(normal, tangent1);
}

inline constexpr void Clear(Vec3& v)
{
    v.mVal[0] = 0.0f;
    v.mVal[1] = 0.0f;
    v.mVal[2] = 0.0f;
}
