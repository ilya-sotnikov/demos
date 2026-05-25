#pragma once

#include "MathCommon.hpp"

inline f32 Vec3::X() const
{
    return val[0];
}

inline f32 Vec3::Y() const
{
    return val[1];
}

inline f32 Vec3::Z() const
{
    return val[2];
}

inline f32 Vec3::R() const
{
    return val[0];
}

inline f32 Vec3::G() const
{
    return val[1];
}

inline f32 Vec3::B() const
{
    return val[2];
}

inline f32& Vec3::X()
{
    return val[0];
}

inline f32& Vec3::Y()
{
    return val[1];
}

inline f32& Vec3::Z()
{
    return val[2];
}

inline f32& Vec3::R()
{
    return val[0];
}

inline f32& Vec3::G()
{
    return val[1];
}

inline f32& Vec3::B()
{
    return val[2];
}

inline f32 Vec3::operator[](int i) const
{
    DEBUG_ASSERT(i >= 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline f32& Vec3::operator[](int i)
{
    DEBUG_ASSERT(i >= 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline Vec3 operator-(Vec3 v)
{
    return {-v.val[0], -v.val[1], -v.val[2]};
}

inline Vec3& operator+=(Vec3& lhs, Vec3 rhs)
{
    lhs.val[0] += rhs.val[0];
    lhs.val[1] += rhs.val[1];
    lhs.val[2] += rhs.val[2];
    return lhs;
}

inline Vec3& operator-=(Vec3& lhs, Vec3 rhs)
{
    lhs.val[0] -= rhs.val[0];
    lhs.val[1] -= rhs.val[1];
    lhs.val[2] -= rhs.val[2];
    return lhs;
}

inline Vec3& operator*=(Vec3& lhs, Vec3 rhs)
{
    lhs.val[0] *= rhs.val[0];
    lhs.val[1] *= rhs.val[1];
    lhs.val[2] *= rhs.val[2];
    return lhs;
}

inline Vec3& operator*=(Vec3& lhs, f32 rhs)
{
    lhs.val[0] *= rhs;
    lhs.val[1] *= rhs;
    lhs.val[2] *= rhs;
    return lhs;
}

inline Vec3& operator/=(Vec3& lhs, f32 rhs)
{
    lhs.val[0] /= rhs;
    lhs.val[1] /= rhs;
    lhs.val[2] /= rhs;
    return lhs;
}

inline Vec3& operator/=(Vec3& lhs, Vec3 rhs)
{
    lhs.val[0] /= rhs.val[0];
    lhs.val[1] /= rhs.val[1];
    lhs.val[2] /= rhs.val[2];
    return lhs;
}

inline Vec3 operator+(Vec3 lhs, Vec3 rhs)
{
    return {lhs.val[0] + rhs.val[0], lhs.val[1] + rhs.val[1], lhs.val[2] + rhs.val[2]};
}

inline Vec3 operator-(Vec3 lhs, Vec3 rhs)
{
    return {lhs.val[0] - rhs.val[0], lhs.val[1] - rhs.val[1], lhs.val[2] - rhs.val[2]};
}

inline Vec3 operator*(Vec3 lhs, Vec3 rhs)
{
    return {lhs.val[0] * rhs.val[0], lhs.val[1] * rhs.val[1], lhs.val[2] * rhs.val[2]};
}

inline Vec3 operator*(Vec3 lhs, f32 rhs)
{
    return {lhs.val[0] * rhs, lhs.val[1] * rhs, lhs.val[2] * rhs};
}

inline Vec3 operator/(Vec3 lhs, f32 rhs)
{
    return {lhs.val[0] / rhs, lhs.val[1] / rhs, lhs.val[2] / rhs};
}

inline Vec3 operator/(Vec3 lhs, Vec3 rhs)
{
    return {lhs.val[0] / rhs.val[0], lhs.val[1] / rhs.val[1], lhs.val[2] / rhs.val[2]};
}

inline Vec3 operator*(f32 lhs, Vec3 rhs)
{
    return {lhs * rhs.val[0], lhs * rhs.val[1], lhs * rhs.val[2]};
}

inline bool operator==(Vec3 lhs, Vec3 rhs)
{
    return (lhs.val[0] == rhs.val[0]) && (lhs.val[1] == rhs.val[1]) && (lhs.val[2] == rhs.val[2]);
}

inline bool operator!=(Vec3 lhs, Vec3 rhs)
{
    return (lhs.val[0] != rhs.val[0]) || (lhs.val[1] != rhs.val[1]) || (lhs.val[2] != rhs.val[2]);
}

inline bool AlmostEqual(Vec3 lhs, Vec3 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.val[0], rhs.val[0], tolerance)
        && AlmostEqual(lhs.val[1], rhs.val[1], tolerance)
        && AlmostEqual(lhs.val[2], rhs.val[2], tolerance);
}

inline Vec3 Abs(Vec3 v)
{
    return {fabsf(v.val[0]), fabsf(v.val[1]), fabsf(v.val[2])};
}

inline f32 Dot(Vec3 a, Vec3 b)
{
    return a.val[0] * b.val[0] + a.val[1] * b.val[1] + a.val[2] * b.val[2];
}

inline f32 MagnitudeSq(Vec3 v)
{
    return v.val[0] * v.val[0] + v.val[1] * v.val[1] + v.val[2] * v.val[2];
}

inline f32 Magnitude(Vec3 v)
{
    return sqrtf(v.val[0] * v.val[0] + v.val[1] * v.val[1] + v.val[2] * v.val[2]);
}

inline Vec3 Normalize(Vec3 v)
{
    const f32 mag = sqrtf(v.val[0] * v.val[0] + v.val[1] * v.val[1] + v.val[2] * v.val[2]);
    DEBUG_ASSERT(mag != 0.0f);
    return {v.val[0] / mag, v.val[1] / mag, v.val[2] / mag};
}

inline Vec3 Cross(Vec3 a, Vec3 b)
{
    const f32 x = a.val[1] * b.val[2] - b.val[1] * a.val[2];
    const f32 y = a.val[2] * b.val[0] - a.val[0] * b.val[2];
    const f32 z = a.val[0] * b.val[1] - b.val[0] * a.val[1];
    return {x, y, z};
}

inline f32 TripleProduct(Vec3 a, Vec3 b, Vec3 c)
{
    const f32 x = a.val[1] * b.val[2] - b.val[1] * a.val[2];
    const f32 y = a.val[2] * b.val[0] - a.val[0] * b.val[2];
    const f32 z = a.val[0] * b.val[1] - b.val[0] * a.val[1];
    return x * c.val[0] + y * c.val[1] + z * c.val[2];
}

inline Vec3 Lerp(Vec3 a, Vec3 b, f32 t)
{
    const f32 x = 1.0f - t;
    return {
        a.val[0] * t + b.val[0] * x,
        a.val[1] * t + b.val[1] * x,
        a.val[2] * t + b.val[2] * x,
    };
}

// https://box2d.org/posts/2014/02/computing-a-basis/
inline void ComputeBasis(Vec3 normal, Vec3& tangent1, Vec3& tangent2)
{
    // Suppose vector a has all equal components and is a unit vector:
    // a = (s, s, s)
    // Then 3*s*s = 1, s = sqrt(1/3) = 0.57735. This means that at
    // least one component of a unit vector must be greater or equal
    // to 0.57735.
    if (fabsf(normal.val[0]) >= 0.57735f)
    {
        tangent1 = {normal.val[1], -normal.val[0], 0.0f};
    }
    else
    {
        tangent1 = {0.0f, normal.val[2], -normal.val[1]};
    }

    tangent1 = Normalize(tangent1);
    tangent2 = Cross(normal, tangent1);
}

inline void Clear(Vec3& v)
{
    v.val[0] = 0.0f;
    v.val[1] = 0.0f;
    v.val[2] = 0.0f;
}
