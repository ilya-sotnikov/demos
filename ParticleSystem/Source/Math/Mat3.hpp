#pragma once

#include "MathCommon.hpp"
#include "Vec3.hpp"

inline constexpr Mat3 Mat3::Identity()
{
    Mat3 m{};
    m.col[0].val[0] = 1.0f;
    m.col[1].val[1] = 1.0f;
    m.col[2].val[2] = 1.0f;
    return m;
}

inline constexpr Mat3 Mat3::Zero()
{
    return {};
}

inline constexpr f32 Mat3::operator()(int row, int column) const
{
    DEBUG_ASSERT(row >= 0);
    DEBUG_ASSERT(column >= 0);
    DEBUG_ASSERT(row < N);
    DEBUG_ASSERT(column < N);
    return col[column].val[row];
}

inline constexpr f32& Mat3::operator()(int row, int column)
{
    DEBUG_ASSERT(row >= 0);
    DEBUG_ASSERT(column >= 0);
    DEBUG_ASSERT(row < N);
    DEBUG_ASSERT(column < N);
    return col[column].val[row];
}

inline constexpr Mat3 operator-(const Mat3& v)
{
    // clang-format off
    return {
        -v.col[0],
        -v.col[1],
        -v.col[2]
    };
    // clang-format on
}

inline constexpr Mat3& operator+=(Mat3& lhs, const Mat3& rhs)
{
    lhs.col[0] += rhs.col[0];
    lhs.col[1] += rhs.col[1];
    lhs.col[2] += rhs.col[2];
    return lhs;
}

inline constexpr Mat3& operator-=(Mat3& lhs, const Mat3& rhs)
{
    lhs.col[0] -= rhs.col[0];
    lhs.col[1] -= rhs.col[1];
    lhs.col[2] -= rhs.col[2];
    return lhs;
}

inline constexpr Mat3& operator*=(Mat3& lhs, f32 rhs)
{
    lhs.col[0] *= rhs;
    lhs.col[1] *= rhs;
    lhs.col[2] *= rhs;
    return lhs;
}

inline constexpr Mat3& operator/=(Mat3& lhs, f32 rhs)
{
    lhs.col[0] /= rhs;
    lhs.col[1] /= rhs;
    lhs.col[2] /= rhs;
    return lhs;
}

inline constexpr Mat3 operator+(const Mat3& lhs, const Mat3& rhs)
{
    return {lhs.col[0] + rhs.col[0], lhs.col[1] + rhs.col[1], lhs.col[2] + rhs.col[2]};
}

inline constexpr Mat3 operator-(const Mat3& lhs, const Mat3& rhs)
{
    return {lhs.col[0] - rhs.col[0], lhs.col[1] - rhs.col[1], lhs.col[2] - rhs.col[2]};
}

inline constexpr Mat3 operator*(const Mat3& lhs, f32 rhs)
{
    return {lhs.col[0] * rhs, lhs.col[1] * rhs, lhs.col[2] * rhs};
}

inline constexpr Mat3 operator/(const Mat3& lhs, f32 rhs)
{
    return {lhs.col[0] / rhs, lhs.col[1] / rhs, lhs.col[2] / rhs};
}

inline constexpr Mat3 operator*(f32 lhs, const Mat3& rhs)
{
    return {lhs * rhs.col[0], lhs * rhs.col[1], lhs * rhs.col[2]};
}

inline constexpr Vec3 operator*(const Mat3& lhs, Vec3 rhs)
{
    // clang-format off
    return {
        lhs.col[0].val[0] * rhs.val[0] +
        lhs.col[1].val[0] * rhs.val[1] +
        lhs.col[2].val[0] * rhs.val[2],

        lhs.col[0].val[1] * rhs.val[0] +
        lhs.col[1].val[1] * rhs.val[1] +
        lhs.col[2].val[1] * rhs.val[2],

        lhs.col[0].val[2] * rhs.val[0] +
        lhs.col[1].val[2] * rhs.val[1] +
        lhs.col[2].val[2] * rhs.val[2]
    };
    // clang-format on
}

inline constexpr Mat3 operator*(const Mat3& lhs, const Mat3& rhs)
{
    return {lhs * rhs.col[0], lhs * rhs.col[1], lhs * rhs.col[2]};
}

inline constexpr Vec3 TMul(const Mat3& lhsT, Vec3 rhs)
{
    // clang-format off
    return {
        lhsT.col[0].val[0] * rhs.val[0] +
        lhsT.col[0].val[1] * rhs.val[1] +
        lhsT.col[0].val[2] * rhs.val[2],

        lhsT.col[1].val[0] * rhs.val[0] +
        lhsT.col[1].val[1] * rhs.val[1] +
        lhsT.col[1].val[2] * rhs.val[2],

        lhsT.col[2].val[0] * rhs.val[0] +
        lhsT.col[2].val[1] * rhs.val[1] +
        lhsT.col[2].val[2] * rhs.val[2]
    };
    // clang-format on
}

inline Mat3 TMul(const Mat3& lhs, const Mat3& rhs)
{
    Mat3 res;
    for (int i = 0; i < Mat3::N; ++i)
    {
        for (int j = 0; j < Mat3::N; ++j)
        {
            res.col[i].val[j] = Dot(lhs.col[j], rhs.col[i]);
        }
    }
    return res;
}

[[nodiscard]]
inline constexpr bool operator==(const Mat3& lhs, const Mat3& rhs)
{
    return (lhs.col[0] == rhs.col[0]) && (lhs.col[1] == rhs.col[1]) && (lhs.col[2] == rhs.col[2]);
}

[[nodiscard]]
inline constexpr bool operator!=(const Mat3& lhs, const Mat3& rhs)
{
    return (lhs.col[0] != rhs.col[0]) || (lhs.col[1] != rhs.col[1]) || (lhs.col[2] != rhs.col[2]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Mat3 lhs, Mat3 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.col[0], rhs.col[0], tolerance)
        && AlmostEqual(lhs.col[1], rhs.col[1], tolerance)
        && AlmostEqual(lhs.col[2], rhs.col[2], tolerance);
}

inline constexpr Mat3 Abs(const Mat3& m)
{
    return {Abs(m.col[0]), Abs(m.col[1]), Abs(m.col[2])};
}

[[nodiscard]]
inline constexpr f32 Determinant(const Mat3& m)
{
    const f32 m11 = m.col[1].val[1];
    const f32 m01 = m.col[0].val[1];
    const f32 m02 = m.col[0].val[2];

    const f32 m21 = m.col[2].val[1];
    const f32 m22 = m.col[2].val[2];
    const f32 m12 = m.col[1].val[2];

    // clang-format off
    return
        m.col[0].val[0] * (m11 * m22 - m12 * m21) -
        m.col[1].val[0] * (m01 * m22 - m02 * m21) +
        m.col[2].val[0] * (m01 * m12 - m02 * m11);
    // clang-format on
}

inline constexpr Mat3 Transpose(const Mat3& m)
{
    return {
        Vec3{m.col[0].val[0], m.col[1].val[0], m.col[2].val[0]},
        Vec3{m.col[0].val[1], m.col[1].val[1], m.col[2].val[1]},
        Vec3{m.col[0].val[2], m.col[1].val[2], m.col[2].val[2]},
    };
}

inline constexpr Mat3 Inverse(const Mat3& m)
{
    // a00 a01 a02
    // a10 a11 a12
    // a20 a21 a22

    // clang-format off
    const f32 a00 =  (m.col[1].val[1] * m.col[2].val[2] -
                      m.col[1].val[2] * m.col[2].val[1]);
    const f32 a01 = -(m.col[0].val[1] * m.col[2].val[2] -
                      m.col[0].val[2] * m.col[2].val[1]);
    const f32 a02 =  (m.col[0].val[1] * m.col[1].val[2] -
                      m.col[0].val[2] * m.col[1].val[1]);

    const f32 a10 = -(m.col[1].val[0] * m.col[2].val[2] -
                      m.col[1].val[2] * m.col[2].val[0]);
    const f32 a11 =  (m.col[0].val[0] * m.col[2].val[2] -
                      m.col[0].val[2] * m.col[2].val[0]);
    const f32 a12 = -(m.col[0].val[0] * m.col[1].val[2] -
                      m.col[0].val[2] * m.col[1].val[0]);

    const f32 a20 =  (m.col[1].val[0] * m.col[2].val[1] -
                      m.col[1].val[1] * m.col[2].val[0]);
    const f32 a21 = -(m.col[0].val[0] * m.col[2].val[1] -
                      m.col[0].val[1] * m.col[2].val[0]);
    const f32 a22 =  (m.col[0].val[0] * m.col[1].val[1] -
                      m.col[0].val[1] * m.col[1].val[0]);

    const f32 det = m.col[0].val[0] * a00 +
                    m.col[1].val[0] * a01 +
                    m.col[2].val[0] * a02;
    DEBUG_ASSERT(det != 0.0f);
    const f32 invDet = 1.0f / det;

    return {
        Vec3{a00 * invDet, a01 * invDet, a02 * invDet},
        Vec3{a10 * invDet, a11 * invDet, a12 * invDet},
        Vec3{a20 * invDet, a21 * invDet, a22 * invDet}
    };
    // clang-format on
}
