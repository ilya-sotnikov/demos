#pragma once

#include "MathCommon.hpp"
#include "Vec2.hpp"

inline constexpr Mat2 Mat2::Identity()
{
    return {1.0f, 0.0f, 0.0f, 1.0f};
}

inline constexpr Mat2 Mat2::Zero()
{
    return {};
}

inline constexpr Mat2 Mat2::FromAngle(f32 angle)
{
    const f32 c = cosf(angle);
    const f32 s = sinf(angle);
    return {c, s, -s, c};
}

inline constexpr f32 Mat2::operator()(int row, int col) const
{
    assert(row >= 0);
    assert(col >= 0);
    assert(row < N);
    assert(col < N);
    return mCol[col].mVal[row];
}

inline constexpr f32& Mat2::operator()(int row, int col)
{
    assert(row >= 0);
    assert(col >= 0);
    assert(row < N);
    assert(col < N);
    return mCol[col].mVal[row];
}

inline constexpr Mat2 operator-(Mat2 m)
{
    return {-m.mCol[0], -m.mCol[1]};
}

inline constexpr Mat2& operator+=(Mat2& lhs, Mat2 rhs)
{
    lhs.mCol[0] += rhs.mCol[0];
    lhs.mCol[1] += rhs.mCol[1];
    return lhs;
}

inline constexpr Mat2& operator-=(Mat2& lhs, Mat2 rhs)
{
    lhs.mCol[0] -= rhs.mCol[0];
    lhs.mCol[1] -= rhs.mCol[1];
    return lhs;
}

inline constexpr Mat2& operator*=(Mat2& lhs, f32 rhs)
{
    lhs.mCol[0] *= rhs;
    lhs.mCol[1] *= rhs;
    return lhs;
}

inline constexpr Mat2& operator/=(Mat2& lhs, f32 rhs)
{
    lhs.mCol[0] /= rhs;
    lhs.mCol[1] /= rhs;
    return lhs;
}

inline constexpr Mat2 operator+(Mat2 lhs, Mat2 rhs)
{
    return {lhs.mCol[0] + rhs.mCol[0], lhs.mCol[1] + rhs.mCol[1]};
}

inline constexpr Mat2 operator-(Mat2 lhs, Mat2 rhs)
{
    return {lhs.mCol[0] - rhs.mCol[0], lhs.mCol[1] - rhs.mCol[1]};
}

inline constexpr Mat2 operator*(Mat2 lhs, f32 rhs)
{
    return {lhs.mCol[0] * rhs, lhs.mCol[1] * rhs};
}

inline constexpr Mat2 operator/(Mat2 lhs, f32 rhs)
{
    return {lhs.mCol[0] / rhs, lhs.mCol[1] / rhs};
}

inline constexpr Mat2 operator*(f32 lhs, Mat2 rhs)
{
    return {lhs * rhs.mCol[0], lhs * rhs.mCol[1]};
}

inline constexpr Vec2 operator*(Mat2 lhs, Vec2 rhs)
{
    // clang-format off
    return {
        lhs.mCol[0].mVal[0] * rhs.mVal[0] + lhs.mCol[1].mVal[0] * rhs.mVal[1],
        lhs.mCol[0].mVal[1] * rhs.mVal[0] + lhs.mCol[1].mVal[1] * rhs.mVal[1]
    };
    // clang-format on
}

inline constexpr Mat2 operator*(Mat2 lhs, Mat2 rhs)
{
    // clang-format off
    return {
        lhs.mCol[0].mVal[0] * rhs.mCol[0].mVal[0] +
        lhs.mCol[1].mVal[0] * rhs.mCol[0].mVal[1],

        lhs.mCol[0].mVal[1] * rhs.mCol[0].mVal[0] +
        lhs.mCol[1].mVal[1] * rhs.mCol[0].mVal[1],

        lhs.mCol[0].mVal[0] * rhs.mCol[1].mVal[0] +
        lhs.mCol[1].mVal[0] * rhs.mCol[1].mVal[1],

        lhs.mCol[0].mVal[1] * rhs.mCol[1].mVal[0] +
        lhs.mCol[1].mVal[1] * rhs.mCol[1].mVal[1]
    };
    // clang-format on
}

inline constexpr Vec2 TMul(Mat2 lhsT, Vec2 rhs)
{
    // clang-format off
    return {
        lhsT.mCol[0].mVal[0] * rhs.mVal[0] + lhsT.mCol[0].mVal[1] * rhs.mVal[1],
        lhsT.mCol[1].mVal[0] * rhs.mVal[0] + lhsT.mCol[1].mVal[1] * rhs.mVal[1]
    };
    // clang-format on
}

inline constexpr Mat2 TMul(Mat2 lhsT, Mat2 rhs)
{
    // clang-format off
    return {
        lhsT.mCol[0].mVal[0] * rhs.mCol[0].mVal[0] +
        lhsT.mCol[0].mVal[1] * rhs.mCol[0].mVal[1],

        lhsT.mCol[1].mVal[0] * rhs.mCol[0].mVal[0] +
        lhsT.mCol[1].mVal[1] * rhs.mCol[0].mVal[1],

        lhsT.mCol[0].mVal[0] * rhs.mCol[1].mVal[0] +
        lhsT.mCol[0].mVal[1] * rhs.mCol[1].mVal[1],

        lhsT.mCol[1].mVal[0] * rhs.mCol[1].mVal[0] +
        lhsT.mCol[1].mVal[1] * rhs.mCol[1].mVal[1]
    };
    // clang-format on
}

[[nodiscard]]
inline constexpr bool operator==(Mat2 lhs, Mat2 rhs)
{
    return (lhs.mCol[0] == rhs.mCol[0]) && (lhs.mCol[1] == rhs.mCol[1]);
}

[[nodiscard]]
inline constexpr bool operator!=(Mat2 lhs, Mat2 rhs)
{
    return (lhs.mCol[0] != rhs.mCol[0]) || (lhs.mCol[1] != rhs.mCol[1]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Mat2 lhs, Mat2 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.mCol[0], rhs.mCol[0], tolerance)
        && AlmostEqual(lhs.mCol[1], rhs.mCol[1], tolerance);
}

inline constexpr Mat2 Abs(Mat2 m)
{
    return {Abs(m.mCol[0]), Abs(m.mCol[1])};
}

[[nodiscard]]
inline constexpr f32 Determinant(Mat2 m)
{
    // clang-format off
    return
        m.mCol[0].mVal[0] * m.mCol[1].mVal[1] - m.mCol[1].mVal[0] * m.mCol[0].mVal[1];
    // clang-format on
}

inline constexpr Mat2 Transpose(Mat2 m)
{
    return {m.mCol[0].mVal[0], m.mCol[1].mVal[0], m.mCol[0].mVal[1], m.mCol[1].mVal[1]};
}

inline constexpr Mat2 Inverse(Mat2 m)
{
    const f32 det = Determinant(m);
    assert(det != 0.0f);
    const f32 invDet = 1.0f / det;
    return {
        Vec2{m.mCol[1].mVal[1] * invDet, -m.mCol[1].mVal[0] * invDet},
        Vec2{-m.mCol[0].mVal[1] * invDet, m.mCol[0].mVal[0] * invDet}
    };
}
