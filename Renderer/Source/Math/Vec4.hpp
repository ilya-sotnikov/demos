#pragma once

#include "MathCommon.hpp"

inline constexpr f32 Vec4::X() const
{
    return val[0];
}

inline constexpr f32 Vec4::Y() const
{
    return val[1];
}

inline constexpr f32 Vec4::Z() const
{
    return val[2];
}

inline constexpr f32 Vec4::W() const
{
    return val[3];
}

inline constexpr f32 Vec4::R() const
{
    return val[0];
}

inline constexpr f32 Vec4::G() const
{
    return val[1];
}

inline constexpr f32 Vec4::B() const
{
    return val[2];
}

inline constexpr f32 Vec4::A() const
{
    return val[3];
}

inline constexpr f32& Vec4::X()
{
    return val[0];
}

inline constexpr f32& Vec4::Y()
{
    return val[1];
}

inline constexpr f32& Vec4::Z()
{
    return val[2];
}

inline constexpr f32& Vec4::W()
{
    return val[3];
}

inline constexpr f32& Vec4::R()
{
    return val[0];
}

inline constexpr f32& Vec4::G()
{
    return val[1];
}

inline constexpr f32& Vec4::B()
{
    return val[2];
}

inline constexpr f32& Vec4::A()
{
    return val[3];
}

inline constexpr Vec2 Vec4::XY() const
{
    return {val[0], val[1]};
}

inline constexpr Vec3 Vec4::XYZ() const
{
    return {val[0], val[1], val[2]};
}

inline constexpr Vec3 Vec4::RGB() const
{
    return {val[0], val[1], val[2]};
}

inline constexpr f32 Vec4::operator[](int i) const
{
    DEBUG_ASSERT(i >= 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline constexpr f32& Vec4::operator[](int i)
{
    DEBUG_ASSERT(i >= 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline constexpr Vec4 operator-(Vec4 v)
{
    return {-v.val[0], -v.val[1], -v.val[2], -v.val[3]};
}

inline constexpr Vec4& operator+=(Vec4& lhs, Vec4 rhs)
{
    lhs.val[0] += rhs.val[0];
    lhs.val[1] += rhs.val[1];
    lhs.val[2] += rhs.val[2];
    lhs.val[3] += rhs.val[3];
    return lhs;
}

inline constexpr Vec4& operator-=(Vec4& lhs, Vec4 rhs)
{
    lhs.val[0] -= rhs.val[0];
    lhs.val[1] -= rhs.val[1];
    lhs.val[2] -= rhs.val[2];
    lhs.val[3] -= rhs.val[3];
    return lhs;
}

inline constexpr Vec4& operator*=(Vec4& lhs, Vec4 rhs)
{
    lhs.val[0] *= rhs.val[0];
    lhs.val[1] *= rhs.val[1];
    lhs.val[2] *= rhs.val[2];
    lhs.val[3] *= rhs.val[3];
    return lhs;
}

inline constexpr Vec4& operator*=(Vec4& lhs, f32 rhs)
{
    lhs.val[0] *= rhs;
    lhs.val[1] *= rhs;
    lhs.val[2] *= rhs;
    lhs.val[3] *= rhs;
    return lhs;
}

inline constexpr Vec4& operator/=(Vec4& lhs, f32 rhs)
{
    lhs.val[0] /= rhs;
    lhs.val[1] /= rhs;
    lhs.val[2] /= rhs;
    lhs.val[3] /= rhs;
    return lhs;
}

inline constexpr Vec4 operator+(Vec4 lhs, Vec4 rhs)
{
    return {
        lhs.val[0] + rhs.val[0],
        lhs.val[1] + rhs.val[1],
        lhs.val[2] + rhs.val[2],
        lhs.val[3] + rhs.val[3],
    };
}

inline constexpr Vec4 operator-(Vec4 lhs, Vec4 rhs)
{
    return {
        lhs.val[0] - rhs.val[0],
        lhs.val[1] - rhs.val[1],
        lhs.val[2] - rhs.val[2],
        lhs.val[3] - rhs.val[3],
    };
}

inline constexpr Vec4 operator*(Vec4 lhs, Vec4 rhs)
{
    return {
        lhs.val[0] * rhs.val[0],
        lhs.val[1] * rhs.val[1],
        lhs.val[2] * rhs.val[2],
        lhs.val[3] * rhs.val[3],
    };
}

inline constexpr Vec4 operator*(Vec4 lhs, f32 rhs)
{
    return {
        lhs.val[0] * rhs,
        lhs.val[1] * rhs,
        lhs.val[2] * rhs,
        lhs.val[3] * rhs,
    };
}

inline constexpr Vec4 operator/(Vec4 lhs, f32 rhs)
{
    return {
        lhs.val[0] / rhs,
        lhs.val[1] / rhs,
        lhs.val[2] / rhs,
        lhs.val[3] / rhs,
    };
}

inline constexpr Vec4 operator*(f32 lhs, Vec4 rhs)
{
    return {
        lhs * rhs.val[0],
        lhs * rhs.val[1],
        lhs * rhs.val[2],
        lhs * rhs.val[3],
    };
}

[[nodiscard]]
inline constexpr bool operator==(Vec4 lhs, Vec4 rhs)
{
    return (lhs.val[0] == rhs.val[0]) && (lhs.val[1] == rhs.val[1]) && (lhs.val[2] == rhs.val[2])
        && (lhs.val[3] == rhs.val[3]);
}

[[nodiscard]]
inline constexpr bool operator!=(Vec4 lhs, Vec4 rhs)
{
    return (lhs.val[0] != rhs.val[0]) || (lhs.val[1] != rhs.val[1]) || (lhs.val[2] != rhs.val[2])
        || (lhs.val[3] != lhs.val[3]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Vec4 lhs, Vec4 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.val[0], rhs.val[0], tolerance)
        && AlmostEqual(lhs.val[1], rhs.val[1], tolerance)
        && AlmostEqual(lhs.val[2], rhs.val[2], tolerance)
        && AlmostEqual(lhs.val[3], rhs.val[3], tolerance);
}

inline constexpr Vec4 Abs(Vec4 v)
{
    return {fabsf(v.val[0]), fabsf(v.val[1]), fabsf(v.val[2]), fabsf(v.val[3])};
}

[[nodiscard]]
inline constexpr f32 Dot(Vec4 a, Vec4 b)
{
    return a.val[0] * b.val[0] + a.val[1] * b.val[1] + a.val[2] * b.val[2] + a.val[3] * b.val[3];
}

[[nodiscard]]
inline constexpr f32 MagnitudeSq(Vec4 v)
{
    return v.val[0] * v.val[0] + v.val[1] * v.val[1] + v.val[2] * v.val[2] + v.val[3] * v.val[3];
}

[[nodiscard]]
inline constexpr f32 Magnitude(Vec4 v)
{
    return sqrtf(
        v.val[0] * v.val[0] + v.val[1] * v.val[1] + v.val[2] * v.val[2] + v.val[3] * v.val[3]
    );
}

inline constexpr Vec4 Normalize(Vec4 v)
{
    const f32 mag = sqrtf(
        v.val[0] * v.val[0] + v.val[1] * v.val[1] + v.val[2] * v.val[2] + v.val[3] * v.val[3]
    );
    DEBUG_ASSERT(mag != 0.0f);
    return {v.val[0] / mag, v.val[1] / mag, v.val[2] / mag, v.val[3] / mag};
}

inline constexpr Vec4 NormalizePlane(Vec4 p)
{
    const f32 mag = sqrtf(p.val[0] * p.val[0] + p.val[1] * p.val[1] + p.val[2] * p.val[2]);
    DEBUG_ASSERT(mag != 0.0f);
    return {p.val[0] / mag, p.val[1] / mag, p.val[2] / mag, p.val[3] / mag};
}

inline constexpr void Clear(Vec4& v)
{
    v.val[0] = 0.0f;
    v.val[1] = 0.0f;
    v.val[2] = 0.0f;
    v.val[3] = 0.0f;
}
