#pragma once

#include "MathCommon.hpp"

inline constexpr f32 Vec4::X() const
{
    return mVal[0];
}

inline constexpr f32 Vec4::Y() const
{
    return mVal[1];
}

inline constexpr f32 Vec4::Z() const
{
    return mVal[2];
}

inline constexpr f32 Vec4::W() const
{
    return mVal[3];
}

inline constexpr f32 Vec4::R() const
{
    return mVal[0];
}

inline constexpr f32 Vec4::G() const
{
    return mVal[1];
}

inline constexpr f32 Vec4::B() const
{
    return mVal[2];
}

inline constexpr f32 Vec4::A() const
{
    return mVal[3];
}

inline constexpr f32& Vec4::X()
{
    return mVal[0];
}

inline constexpr f32& Vec4::Y()
{
    return mVal[1];
}

inline constexpr f32& Vec4::Z()
{
    return mVal[2];
}

inline constexpr f32& Vec4::W()
{
    return mVal[3];
}

inline constexpr f32& Vec4::R()
{
    return mVal[0];
}

inline constexpr f32& Vec4::G()
{
    return mVal[1];
}

inline constexpr f32& Vec4::B()
{
    return mVal[2];
}

inline constexpr f32& Vec4::A()
{
    return mVal[3];
}

inline constexpr Vec2 Vec4::XY() const
{
    return {mVal[0], mVal[1]};
}

inline constexpr Vec3 Vec4::XYZ() const
{
    return {mVal[0], mVal[1], mVal[2]};
}

inline constexpr Vec3 Vec4::RGB() const
{
    return {mVal[0], mVal[1], mVal[2]};
}

inline constexpr f32 Vec4::operator[](int i) const
{
    assert(i >= 0);
    assert(i < N);
    return mVal[i];
}

inline constexpr f32& Vec4::operator[](int i)
{
    assert(i >= 0);
    assert(i < N);
    return mVal[i];
}

inline constexpr Vec4 operator-(Vec4 v)
{
    return {-v.mVal[0], -v.mVal[1], -v.mVal[2], -v.mVal[3]};
}

inline constexpr Vec4& operator+=(Vec4& lhs, Vec4 rhs)
{
    lhs.mVal[0] += rhs.mVal[0];
    lhs.mVal[1] += rhs.mVal[1];
    lhs.mVal[2] += rhs.mVal[2];
    lhs.mVal[3] += rhs.mVal[3];
    return lhs;
}

inline constexpr Vec4& operator-=(Vec4& lhs, Vec4 rhs)
{
    lhs.mVal[0] -= rhs.mVal[0];
    lhs.mVal[1] -= rhs.mVal[1];
    lhs.mVal[2] -= rhs.mVal[2];
    lhs.mVal[3] -= rhs.mVal[3];
    return lhs;
}

inline constexpr Vec4& operator*=(Vec4& lhs, Vec4 rhs)
{
    lhs.mVal[0] *= rhs.mVal[0];
    lhs.mVal[1] *= rhs.mVal[1];
    lhs.mVal[2] *= rhs.mVal[2];
    lhs.mVal[3] *= rhs.mVal[3];
    return lhs;
}

inline constexpr Vec4& operator*=(Vec4& lhs, f32 rhs)
{
    lhs.mVal[0] *= rhs;
    lhs.mVal[1] *= rhs;
    lhs.mVal[2] *= rhs;
    lhs.mVal[3] *= rhs;
    return lhs;
}

inline constexpr Vec4& operator/=(Vec4& lhs, f32 rhs)
{
    lhs.mVal[0] /= rhs;
    lhs.mVal[1] /= rhs;
    lhs.mVal[2] /= rhs;
    lhs.mVal[3] /= rhs;
    return lhs;
}

inline constexpr Vec4 operator+(Vec4 lhs, Vec4 rhs)
{
    return {
        lhs.mVal[0] + rhs.mVal[0],
        lhs.mVal[1] + rhs.mVal[1],
        lhs.mVal[2] + rhs.mVal[2],
        lhs.mVal[3] + rhs.mVal[3],
    };
}

inline constexpr Vec4 operator-(Vec4 lhs, Vec4 rhs)
{
    return {
        lhs.mVal[0] - rhs.mVal[0],
        lhs.mVal[1] - rhs.mVal[1],
        lhs.mVal[2] - rhs.mVal[2],
        lhs.mVal[3] - rhs.mVal[3],
    };
}

inline constexpr Vec4 operator*(Vec4 lhs, Vec4 rhs)
{
    return {
        lhs.mVal[0] * rhs.mVal[0],
        lhs.mVal[1] * rhs.mVal[1],
        lhs.mVal[2] * rhs.mVal[2],
        lhs.mVal[3] * rhs.mVal[3],
    };
}

inline constexpr Vec4 operator*(Vec4 lhs, f32 rhs)
{
    return {
        lhs.mVal[0] * rhs,
        lhs.mVal[1] * rhs,
        lhs.mVal[2] * rhs,
        lhs.mVal[3] * rhs,
    };
}

inline constexpr Vec4 operator/(Vec4 lhs, f32 rhs)
{
    return {
        lhs.mVal[0] / rhs,
        lhs.mVal[1] / rhs,
        lhs.mVal[2] / rhs,
        lhs.mVal[3] / rhs,
    };
}

inline constexpr Vec4 operator*(f32 lhs, Vec4 rhs)
{
    return {
        lhs * rhs.mVal[0],
        lhs * rhs.mVal[1],
        lhs * rhs.mVal[2],
        lhs * rhs.mVal[3],
    };
}

[[nodiscard]]
inline constexpr bool operator==(Vec4 lhs, Vec4 rhs)
{
    return (lhs.mVal[0] == rhs.mVal[0]) && (lhs.mVal[1] == rhs.mVal[1])
        && (lhs.mVal[2] == rhs.mVal[2]) && (lhs.mVal[3] == rhs.mVal[3]);
}

[[nodiscard]]
inline constexpr bool operator!=(Vec4 lhs, Vec4 rhs)
{
    return (lhs.mVal[0] != rhs.mVal[0]) || (lhs.mVal[1] != rhs.mVal[1])
        || (lhs.mVal[2] != rhs.mVal[2]) || (lhs.mVal[3] != lhs.mVal[3]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Vec4 lhs, Vec4 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.mVal[0], rhs.mVal[0], tolerance)
        && AlmostEqual(lhs.mVal[1], rhs.mVal[1], tolerance)
        && AlmostEqual(lhs.mVal[2], rhs.mVal[2], tolerance)
        && AlmostEqual(lhs.mVal[3], rhs.mVal[3], tolerance);
}

inline constexpr Vec4 Abs(Vec4 v)
{
    return {fabsf(v.mVal[0]), fabsf(v.mVal[1]), fabsf(v.mVal[2]), fabsf(v.mVal[3])};
}

[[nodiscard]]
inline constexpr f32 Dot(Vec4 a, Vec4 b)
{
    return a.mVal[0] * b.mVal[0] + a.mVal[1] * b.mVal[1] + a.mVal[2] * b.mVal[2]
        + a.mVal[3] * b.mVal[3];
}

[[nodiscard]]
inline constexpr f32 MagnitudeSq(Vec4 v)
{
    return v.mVal[0] * v.mVal[0] + v.mVal[1] * v.mVal[1] + v.mVal[2] * v.mVal[2]
        + v.mVal[3] * v.mVal[3];
}

[[nodiscard]]
inline constexpr f32 Magnitude(Vec4 v)
{
    return sqrtf(
        v.mVal[0] * v.mVal[0] + v.mVal[1] * v.mVal[1] + v.mVal[2] * v.mVal[2]
        + v.mVal[3] * v.mVal[3]
    );
}

inline constexpr Vec4 Normalize(Vec4 v)
{
    const f32 mag = sqrtf(
        v.mVal[0] * v.mVal[0] + v.mVal[1] * v.mVal[1] + v.mVal[2] * v.mVal[2]
        + v.mVal[3] * v.mVal[3]
    );
    assert(mag != 0.0f);
    return {v.mVal[0] / mag, v.mVal[1] / mag, v.mVal[2] / mag, v.mVal[3] / mag};
}

inline constexpr void Clear(Vec4& v)
{
    v.mVal[0] = 0.0f;
    v.mVal[1] = 0.0f;
    v.mVal[2] = 0.0f;
    v.mVal[3] = 0.0f;
}
