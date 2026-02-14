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

inline constexpr f32 Mat2::operator()(int row, int column) const
{
    DEBUG_ASSERT(row >= 0);
    DEBUG_ASSERT(column >= 0);
    DEBUG_ASSERT(row < N);
    DEBUG_ASSERT(column < N);
    return col[column].val[row];
}

inline constexpr f32& Mat2::operator()(int row, int column)
{
    DEBUG_ASSERT(row >= 0);
    DEBUG_ASSERT(column >= 0);
    DEBUG_ASSERT(row < N);
    DEBUG_ASSERT(column < N);
    return col[column].val[row];
}

inline constexpr Mat2 operator-(Mat2 m)
{
    return {-m.col[0], -m.col[1]};
}

inline constexpr Mat2& operator+=(Mat2& lhs, Mat2 rhs)
{
    lhs.col[0] += rhs.col[0];
    lhs.col[1] += rhs.col[1];
    return lhs;
}

inline constexpr Mat2& operator-=(Mat2& lhs, Mat2 rhs)
{
    lhs.col[0] -= rhs.col[0];
    lhs.col[1] -= rhs.col[1];
    return lhs;
}

inline constexpr Mat2& operator*=(Mat2& lhs, f32 rhs)
{
    lhs.col[0] *= rhs;
    lhs.col[1] *= rhs;
    return lhs;
}

inline constexpr Mat2& operator/=(Mat2& lhs, f32 rhs)
{
    lhs.col[0] /= rhs;
    lhs.col[1] /= rhs;
    return lhs;
}

inline constexpr Mat2 operator+(Mat2 lhs, Mat2 rhs)
{
    return {lhs.col[0] + rhs.col[0], lhs.col[1] + rhs.col[1]};
}

inline constexpr Mat2 operator-(Mat2 lhs, Mat2 rhs)
{
    return {lhs.col[0] - rhs.col[0], lhs.col[1] - rhs.col[1]};
}

inline constexpr Mat2 operator*(Mat2 lhs, f32 rhs)
{
    return {lhs.col[0] * rhs, lhs.col[1] * rhs};
}

inline constexpr Mat2 operator/(Mat2 lhs, f32 rhs)
{
    return {lhs.col[0] / rhs, lhs.col[1] / rhs};
}

inline constexpr Mat2 operator*(f32 lhs, Mat2 rhs)
{
    return {lhs * rhs.col[0], lhs * rhs.col[1]};
}

inline constexpr Vec2 operator*(Mat2 lhs, Vec2 rhs)
{
    return {
        lhs.col[0].val[0] * rhs.val[0] + lhs.col[1].val[0] * rhs.val[1],
        lhs.col[0].val[1] * rhs.val[0] + lhs.col[1].val[1] * rhs.val[1]
    };
}

inline constexpr Mat2 operator*(Mat2 lhs, Mat2 rhs)
{
    // clang-format off
    return {
        lhs.col[0].val[0] * rhs.col[0].val[0] +
        lhs.col[1].val[0] * rhs.col[0].val[1],

        lhs.col[0].val[1] * rhs.col[0].val[0] +
        lhs.col[1].val[1] * rhs.col[0].val[1],

        lhs.col[0].val[0] * rhs.col[1].val[0] +
        lhs.col[1].val[0] * rhs.col[1].val[1],

        lhs.col[0].val[1] * rhs.col[1].val[0] +
        lhs.col[1].val[1] * rhs.col[1].val[1]
    };
    // clang-format on
}

inline constexpr Vec2 TMul(Mat2 lhsT, Vec2 rhs)
{
    return {
        lhsT.col[0].val[0] * rhs.val[0] + lhsT.col[0].val[1] * rhs.val[1],
        lhsT.col[1].val[0] * rhs.val[0] + lhsT.col[1].val[1] * rhs.val[1]
    };
}

inline constexpr Mat2 TMul(Mat2 lhsT, Mat2 rhs)
{
    // clang-format off
    return {
        lhsT.col[0].val[0] * rhs.col[0].val[0] +
        lhsT.col[0].val[1] * rhs.col[0].val[1],

        lhsT.col[1].val[0] * rhs.col[0].val[0] +
        lhsT.col[1].val[1] * rhs.col[0].val[1],

        lhsT.col[0].val[0] * rhs.col[1].val[0] +
        lhsT.col[0].val[1] * rhs.col[1].val[1],

        lhsT.col[1].val[0] * rhs.col[1].val[0] +
        lhsT.col[1].val[1] * rhs.col[1].val[1]
    };
    // clang-format on
}

[[nodiscard]]
inline constexpr bool operator==(Mat2 lhs, Mat2 rhs)
{
    return (lhs.col[0] == rhs.col[0]) && (lhs.col[1] == rhs.col[1]);
}

[[nodiscard]]
inline constexpr bool operator!=(Mat2 lhs, Mat2 rhs)
{
    return (lhs.col[0] != rhs.col[0]) || (lhs.col[1] != rhs.col[1]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Mat2 lhs, Mat2 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.col[0], rhs.col[0], tolerance)
        && AlmostEqual(lhs.col[1], rhs.col[1], tolerance);
}

inline constexpr Mat2 Abs(Mat2 m)
{
    return {Abs(m.col[0]), Abs(m.col[1])};
}

[[nodiscard]]
inline constexpr f32 Determinant(Mat2 m)
{
    return m.col[0].val[0] * m.col[1].val[1] - m.col[1].val[0] * m.col[0].val[1];
}

inline constexpr Mat2 Transpose(Mat2 m)
{
    return {m.col[0].val[0], m.col[1].val[0], m.col[0].val[1], m.col[1].val[1]};
}

inline constexpr Mat2 Inverse(Mat2 m)
{
    const f32 det = Determinant(m);
    DEBUG_ASSERT(det != 0.0f);
    const f32 invDet = 1.0f / det;
    return {
        Vec2{m.col[1].val[1] * invDet, -m.col[1].val[0] * invDet},
        Vec2{-m.col[0].val[1] * invDet, m.col[0].val[0] * invDet}
    };
}
