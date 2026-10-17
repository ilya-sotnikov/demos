#pragma once

#include "math_common.hpp"
#include "vec2.hpp"

inline Mat2 Mat2::identity() {
    return {1.0f, 0.0f, 0.0f, 1.0f};
}

inline Mat2 Mat2::zero() {
    return {};
}

inline Mat2 Mat2::from_angle(f32 angle) {
    const f32 c = cosf(angle);
    const f32 s = sinf(angle);
    return {c, s, -s, c};
}

inline f32 Mat2::operator()(int row, int column) const {
    DEBUG_ASSERT(row >= 0);
    DEBUG_ASSERT(column >= 0);
    DEBUG_ASSERT(row < N);
    DEBUG_ASSERT(column < N);
    return col[column].val[row];
}

inline f32& Mat2::operator()(int row, int column) {
    DEBUG_ASSERT(row >= 0);
    DEBUG_ASSERT(column >= 0);
    DEBUG_ASSERT(row < N);
    DEBUG_ASSERT(column < N);
    return col[column].val[row];
}

inline Mat2 operator-(Mat2 m) {
    return {-m.col[0], -m.col[1]};
}

inline Mat2& operator+=(Mat2& lhs, Mat2 rhs) {
    lhs.col[0] += rhs.col[0];
    lhs.col[1] += rhs.col[1];
    return lhs;
}

inline Mat2& operator-=(Mat2& lhs, Mat2 rhs) {
    lhs.col[0] -= rhs.col[0];
    lhs.col[1] -= rhs.col[1];
    return lhs;
}

inline Mat2& operator*=(Mat2& lhs, f32 rhs) {
    lhs.col[0] *= rhs;
    lhs.col[1] *= rhs;
    return lhs;
}

inline Mat2& operator/=(Mat2& lhs, f32 rhs) {
    lhs.col[0] /= rhs;
    lhs.col[1] /= rhs;
    return lhs;
}

inline Mat2 operator+(Mat2 lhs, Mat2 rhs) {
    return {lhs.col[0] + rhs.col[0], lhs.col[1] + rhs.col[1]};
}

inline Mat2 operator-(Mat2 lhs, Mat2 rhs) {
    return {lhs.col[0] - rhs.col[0], lhs.col[1] - rhs.col[1]};
}

inline Mat2 operator*(Mat2 lhs, f32 rhs) {
    return {lhs.col[0] * rhs, lhs.col[1] * rhs};
}

inline Mat2 operator/(Mat2 lhs, f32 rhs) {
    return {lhs.col[0] / rhs, lhs.col[1] / rhs};
}

inline Mat2 operator*(f32 lhs, Mat2 rhs) {
    return {lhs * rhs.col[0], lhs * rhs.col[1]};
}

inline Vec2 operator*(Mat2 lhs, Vec2 rhs) {
    return {
        lhs.col[0].val[0] * rhs.val[0] + lhs.col[1].val[0] * rhs.val[1],
        lhs.col[0].val[1] * rhs.val[0] + lhs.col[1].val[1] * rhs.val[1]
    };
}

inline Mat2 operator*(Mat2 lhs, Mat2 rhs) {
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

inline bool operator==(Mat2 lhs, Mat2 rhs) {
    return (lhs.col[0] == rhs.col[0]) && (lhs.col[1] == rhs.col[1]);
}

inline bool operator!=(Mat2 lhs, Mat2 rhs) {
    return (lhs.col[0] != rhs.col[0]) || (lhs.col[1] != rhs.col[1]);
}

namespace math {

inline Vec2 tmul(Mat2 lhst, Vec2 rhs) {
    return {
        lhst.col[0].val[0] * rhs.val[0] + lhst.col[0].val[1] * rhs.val[1],
        lhst.col[1].val[0] * rhs.val[0] + lhst.col[1].val[1] * rhs.val[1]
    };
}

inline Mat2 tmul(Mat2 lhst, Mat2 rhs) {
    // clang-format off
    return {
        lhst.col[0].val[0] * rhs.col[0].val[0] +
        lhst.col[0].val[1] * rhs.col[0].val[1],

        lhst.col[1].val[0] * rhs.col[0].val[0] +
        lhst.col[1].val[1] * rhs.col[0].val[1],

        lhst.col[0].val[0] * rhs.col[1].val[0] +
        lhst.col[0].val[1] * rhs.col[1].val[1],

        lhst.col[1].val[0] * rhs.col[1].val[0] +
        lhst.col[1].val[1] * rhs.col[1].val[1]
    };
    // clang-format on
}

inline bool almost_equal(Mat2 lhs, Mat2 rhs, f32 tolerance = FLT_EPSILON) {
    return almost_equal(lhs.col[0], rhs.col[0], tolerance)
        && almost_equal(lhs.col[1], rhs.col[1], tolerance);
}

inline Mat2 abs(Mat2 m) {
    return {abs(m.col[0]), abs(m.col[1])};
}

inline f32 determinant(Mat2 m) {
    return m.col[0].val[0] * m.col[1].val[1] - m.col[1].val[0] * m.col[0].val[1];
}

inline Mat2 transpose(Mat2 m) {
    return {m.col[0].val[0], m.col[1].val[0], m.col[0].val[1], m.col[1].val[1]};
}

inline Mat2 inverse(Mat2 m) {
    const f32 det = determinant(m);
    DEBUG_ASSERT(det != 0.0f);
    const f32 inv_det = 1.0f / det;
    return {
        Vec2{m.col[1].val[1] * inv_det, -m.col[1].val[0] * inv_det},
        Vec2{-m.col[0].val[1] * inv_det, m.col[0].val[0] * inv_det}
    };
}

} // namespace math