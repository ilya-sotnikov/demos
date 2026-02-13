#pragma once

#include "MathCommon.hpp"
#include "Vec3.hpp"

inline constexpr Mat3 Mat3::Identity()
{
    Mat3 m{};
    m.mCol[0].mVal[0] = 1.0f;
    m.mCol[1].mVal[1] = 1.0f;
    m.mCol[2].mVal[2] = 1.0f;
    return m;
}

inline constexpr Mat3 Mat3::Zero()
{
    return {};
}

inline constexpr f32 Mat3::operator()(int row, int col) const
{
    assert(row >= 0);
    assert(col >= 0);
    assert(row < N);
    assert(col < N);
    return mCol[col].mVal[row];
}

inline constexpr f32& Mat3::operator()(int row, int col)
{
    assert(row >= 0);
    assert(col >= 0);
    assert(row < N);
    assert(col < N);
    return mCol[col].mVal[row];
}

inline constexpr Mat3 operator-(const Mat3& v)
{
    // clang-format off
    return {
        -v.mCol[0],
        -v.mCol[1],
        -v.mCol[2]
    };
    // clang-format on
}

inline constexpr Mat3& operator+=(Mat3& lhs, const Mat3& rhs)
{
    lhs.mCol[0] += rhs.mCol[0];
    lhs.mCol[1] += rhs.mCol[1];
    lhs.mCol[2] += rhs.mCol[2];
    return lhs;
}

inline constexpr Mat3& operator-=(Mat3& lhs, const Mat3& rhs)
{
    lhs.mCol[0] -= rhs.mCol[0];
    lhs.mCol[1] -= rhs.mCol[1];
    lhs.mCol[2] -= rhs.mCol[2];
    return lhs;
}

inline constexpr Mat3& operator*=(Mat3& lhs, f32 rhs)
{
    lhs.mCol[0] *= rhs;
    lhs.mCol[1] *= rhs;
    lhs.mCol[2] *= rhs;
    return lhs;
}

inline constexpr Mat3& operator/=(Mat3& lhs, f32 rhs)
{
    lhs.mCol[0] /= rhs;
    lhs.mCol[1] /= rhs;
    lhs.mCol[2] /= rhs;
    return lhs;
}

inline constexpr Mat3 operator+(const Mat3& lhs, const Mat3& rhs)
{
    return {lhs.mCol[0] + rhs.mCol[0], lhs.mCol[1] + rhs.mCol[1], lhs.mCol[2] + rhs.mCol[2]};
}

inline constexpr Mat3 operator-(const Mat3& lhs, const Mat3& rhs)
{
    return {lhs.mCol[0] - rhs.mCol[0], lhs.mCol[1] - rhs.mCol[1], lhs.mCol[2] - rhs.mCol[2]};
}

inline constexpr Mat3 operator*(const Mat3& lhs, f32 rhs)
{
    return {lhs.mCol[0] * rhs, lhs.mCol[1] * rhs, lhs.mCol[2] * rhs};
}

inline constexpr Mat3 operator/(const Mat3& lhs, f32 rhs)
{
    return {lhs.mCol[0] / rhs, lhs.mCol[1] / rhs, lhs.mCol[2] / rhs};
}

inline constexpr Mat3 operator*(f32 lhs, const Mat3& rhs)
{
    return {lhs * rhs.mCol[0], lhs * rhs.mCol[1], lhs * rhs.mCol[2]};
}

inline constexpr Vec3 operator*(const Mat3& lhs, Vec3 rhs)
{
    // clang-format off
    return {
        lhs.mCol[0].mVal[0] * rhs.mVal[0] +
        lhs.mCol[1].mVal[0] * rhs.mVal[1] +
        lhs.mCol[2].mVal[0] * rhs.mVal[2],

        lhs.mCol[0].mVal[1] * rhs.mVal[0] +
        lhs.mCol[1].mVal[1] * rhs.mVal[1] +
        lhs.mCol[2].mVal[1] * rhs.mVal[2],

        lhs.mCol[0].mVal[2] * rhs.mVal[0] +
        lhs.mCol[1].mVal[2] * rhs.mVal[1] +
        lhs.mCol[2].mVal[2] * rhs.mVal[2]
    };
    // clang-format on
}

inline constexpr Mat3 operator*(const Mat3& lhs, const Mat3& rhs)
{
    return {lhs * rhs.mCol[0], lhs * rhs.mCol[1], lhs * rhs.mCol[2]};
}

inline constexpr Vec3 TMul(const Mat3& lhsT, Vec3 rhs)
{
    // clang-format off
    return {
        lhsT.mCol[0].mVal[0] * rhs.mVal[0] +
        lhsT.mCol[0].mVal[1] * rhs.mVal[1] +
        lhsT.mCol[0].mVal[2] * rhs.mVal[2],

        lhsT.mCol[1].mVal[0] * rhs.mVal[0] +
        lhsT.mCol[1].mVal[1] * rhs.mVal[1] +
        lhsT.mCol[1].mVal[2] * rhs.mVal[2],

        lhsT.mCol[2].mVal[0] * rhs.mVal[0] +
        lhsT.mCol[2].mVal[1] * rhs.mVal[1] +
        lhsT.mCol[2].mVal[2] * rhs.mVal[2]
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
            res.mCol[i].mVal[j] = Dot(lhs.mCol[j], rhs.mCol[i]);
        }
    }
    return res;
}

[[nodiscard]]
inline constexpr bool operator==(const Mat3& lhs, const Mat3& rhs)
{
    return (lhs.mCol[0] == rhs.mCol[0]) && (lhs.mCol[1] == rhs.mCol[1])
        && (lhs.mCol[2] == rhs.mCol[2]);
}

[[nodiscard]]
inline constexpr bool operator!=(const Mat3& lhs, const Mat3& rhs)
{
    return (lhs.mCol[0] != rhs.mCol[0]) || (lhs.mCol[1] != rhs.mCol[1])
        || (lhs.mCol[2] != rhs.mCol[2]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Mat3 lhs, Mat3 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.mCol[0], rhs.mCol[0], tolerance)
        && AlmostEqual(lhs.mCol[1], rhs.mCol[1], tolerance)
        && AlmostEqual(lhs.mCol[2], rhs.mCol[2], tolerance);
}

inline constexpr Mat3 Abs(const Mat3& m)
{
    return {Abs(m.mCol[0]), Abs(m.mCol[1]), Abs(m.mCol[2])};
}

[[nodiscard]]
inline constexpr f32 Determinant(const Mat3& m)
{
    const f32 m11 = m.mCol[1].mVal[1];
    const f32 m01 = m.mCol[0].mVal[1];
    const f32 m02 = m.mCol[0].mVal[2];

    const f32 m21 = m.mCol[2].mVal[1];
    const f32 m22 = m.mCol[2].mVal[2];
    const f32 m12 = m.mCol[1].mVal[2];

    // clang-format off
    return
        m.mCol[0].mVal[0] * (m11 * m22 - m12 * m21) -
        m.mCol[1].mVal[0] * (m01 * m22 - m02 * m21) +
        m.mCol[2].mVal[0] * (m01 * m12 - m02 * m11);
    // clang-format on
}

inline constexpr Mat3 Transpose(const Mat3& m)
{
    // clang-format off
    return {
        Vec3{m.mCol[0].mVal[0], m.mCol[1].mVal[0], m.mCol[2].mVal[0]},
        Vec3{m.mCol[0].mVal[1], m.mCol[1].mVal[1], m.mCol[2].mVal[1]},
        Vec3{m.mCol[0].mVal[2], m.mCol[1].mVal[2], m.mCol[2].mVal[2]},
    };
    // clang-format on
}

inline constexpr Mat3 Inverse(const Mat3& m)
{
    // a00 a01 a02
    // a10 a11 a12
    // a20 a21 a22

    // clang-format off
    const f32 a00 =  (m.mCol[1].mVal[1] * m.mCol[2].mVal[2] -
                      m.mCol[1].mVal[2] * m.mCol[2].mVal[1]);
    const f32 a01 = -(m.mCol[0].mVal[1] * m.mCol[2].mVal[2] -
                      m.mCol[0].mVal[2] * m.mCol[2].mVal[1]);
    const f32 a02 =  (m.mCol[0].mVal[1] * m.mCol[1].mVal[2] -
                      m.mCol[0].mVal[2] * m.mCol[1].mVal[1]);

    const f32 a10 = -(m.mCol[1].mVal[0] * m.mCol[2].mVal[2] -
                      m.mCol[1].mVal[2] * m.mCol[2].mVal[0]);
    const f32 a11 =  (m.mCol[0].mVal[0] * m.mCol[2].mVal[2] -
                      m.mCol[0].mVal[2] * m.mCol[2].mVal[0]);
    const f32 a12 = -(m.mCol[0].mVal[0] * m.mCol[1].mVal[2] -
                      m.mCol[0].mVal[2] * m.mCol[1].mVal[0]);

    const f32 a20 =  (m.mCol[1].mVal[0] * m.mCol[2].mVal[1] -
                      m.mCol[1].mVal[1] * m.mCol[2].mVal[0]);
    const f32 a21 = -(m.mCol[0].mVal[0] * m.mCol[2].mVal[1] -
                      m.mCol[0].mVal[1] * m.mCol[2].mVal[0]);
    const f32 a22 =  (m.mCol[0].mVal[0] * m.mCol[1].mVal[1] -
                      m.mCol[0].mVal[1] * m.mCol[1].mVal[0]);

    const f32 det = m.mCol[0].mVal[0] * a00 +
                    m.mCol[1].mVal[0] * a01 +
                    m.mCol[2].mVal[0] * a02;
    assert(det != 0.0f);
    const f32 invDet = 1.0f / det;

    return {
        Vec3{a00 * invDet, a01 * invDet, a02 * invDet},
        Vec3{a10 * invDet, a11 * invDet, a12 * invDet},
        Vec3{a20 * invDet, a21 * invDet, a22 * invDet}
    };
    // clang-format on
}
