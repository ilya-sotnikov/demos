#pragma once

#include "math_common.hpp"
#include "vec3.hpp"

inline Mat3 Mat3::identity() {
    Mat3 m{};
    m.col[0].val[0] = 1.0f;
    m.col[1].val[1] = 1.0f;
    m.col[2].val[2] = 1.0f;
    return m;
}

inline Mat3 Mat3::zero() {
    return {};
}

inline f32 Mat3::operator()(int row, int column) const {
    DEBUG_ASSERT(row >= 0);
    DEBUG_ASSERT(column >= 0);
    DEBUG_ASSERT(row < N);
    DEBUG_ASSERT(column < N);
    return col[column].val[row];
}

inline f32& Mat3::operator()(int row, int column) {
    DEBUG_ASSERT(row >= 0);
    DEBUG_ASSERT(column >= 0);
    DEBUG_ASSERT(row < N);
    DEBUG_ASSERT(column < N);
    return col[column].val[row];
}

inline Mat3 operator-(const Mat3& v) {
    // clang-format off
    return {
        -v.col[0],
        -v.col[1],
        -v.col[2]
    };
    // clang-format on
}

inline Mat3& operator+=(Mat3& lhs, const Mat3& rhs) {
    lhs.col[0] += rhs.col[0];
    lhs.col[1] += rhs.col[1];
    lhs.col[2] += rhs.col[2];
    return lhs;
}

inline Mat3& operator-=(Mat3& lhs, const Mat3& rhs) {
    lhs.col[0] -= rhs.col[0];
    lhs.col[1] -= rhs.col[1];
    lhs.col[2] -= rhs.col[2];
    return lhs;
}

inline Mat3& operator*=(Mat3& lhs, f32 rhs) {
    lhs.col[0] *= rhs;
    lhs.col[1] *= rhs;
    lhs.col[2] *= rhs;
    return lhs;
}

inline Mat3& operator/=(Mat3& lhs, f32 rhs) {
    lhs.col[0] /= rhs;
    lhs.col[1] /= rhs;
    lhs.col[2] /= rhs;
    return lhs;
}

inline Mat3 operator+(const Mat3& lhs, const Mat3& rhs) {
    return {lhs.col[0] + rhs.col[0], lhs.col[1] + rhs.col[1], lhs.col[2] + rhs.col[2]};
}

inline Mat3 operator-(const Mat3& lhs, const Mat3& rhs) {
    return {lhs.col[0] - rhs.col[0], lhs.col[1] - rhs.col[1], lhs.col[2] - rhs.col[2]};
}

inline Mat3 operator*(const Mat3& lhs, f32 rhs) {
    return {lhs.col[0] * rhs, lhs.col[1] * rhs, lhs.col[2] * rhs};
}

inline Mat3 operator/(const Mat3& lhs, f32 rhs) {
    return {lhs.col[0] / rhs, lhs.col[1] / rhs, lhs.col[2] / rhs};
}

inline Mat3 operator*(f32 lhs, const Mat3& rhs) {
    return {lhs * rhs.col[0], lhs * rhs.col[1], lhs * rhs.col[2]};
}

inline Vec3 operator*(const Mat3& lhs, Vec3 rhs) {
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

inline Mat3 operator*(const Mat3& lhs, const Mat3& rhs) {
    return {lhs * rhs.col[0], lhs * rhs.col[1], lhs * rhs.col[2]};
}

inline bool operator==(const Mat3& lhs, const Mat3& rhs) {
    return (lhs.col[0] == rhs.col[0]) && (lhs.col[1] == rhs.col[1]) && (lhs.col[2] == rhs.col[2]);
}

inline bool operator!=(const Mat3& lhs, const Mat3& rhs) {
    return (lhs.col[0] != rhs.col[0]) || (lhs.col[1] != rhs.col[1]) || (lhs.col[2] != rhs.col[2]);
}

namespace math {

inline Vec3 tmul(const Mat3& lhst, Vec3 rhs) {
    // clang-format off
    return {
        lhst.col[0].val[0] * rhs.val[0] +
        lhst.col[0].val[1] * rhs.val[1] +
        lhst.col[0].val[2] * rhs.val[2],

        lhst.col[1].val[0] * rhs.val[0] +
        lhst.col[1].val[1] * rhs.val[1] +
        lhst.col[1].val[2] * rhs.val[2],

        lhst.col[2].val[0] * rhs.val[0] +
        lhst.col[2].val[1] * rhs.val[1] +
        lhst.col[2].val[2] * rhs.val[2]
    };
    // clang-format on
}

inline Mat3 tmul(const Mat3& lhst, const Mat3& rhs) {
    Mat3 res;
    for (int i = 0; i < Mat3::N; ++i) {
        for (int j = 0; j < Mat3::N; ++j) {
            res.col[i].val[j] = dot(lhst.col[j], rhs.col[i]);
        }
    }
    return res;
}

inline bool almost_equal(Mat3 lhs, Mat3 rhs, f32 tolerance = FLT_EPSILON) {
    return almost_equal(lhs.col[0], rhs.col[0], tolerance)
        && almost_equal(lhs.col[1], rhs.col[1], tolerance)
        && almost_equal(lhs.col[2], rhs.col[2], tolerance);
}

inline Mat3 abs(const Mat3& m) {
    return {abs(m.col[0]), abs(m.col[1]), abs(m.col[2])};
}

inline f32 determinant(const Mat3& m) {
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

inline Mat3 transpose(const Mat3& m) {
    return {
        Vec3{m.col[0].val[0], m.col[1].val[0], m.col[2].val[0]},
        Vec3{m.col[0].val[1], m.col[1].val[1], m.col[2].val[1]},
        Vec3{m.col[0].val[2], m.col[1].val[2], m.col[2].val[2]},
    };
}

inline Mat3 inverse(const Mat3& m) {
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
    const f32 inv_det = 1.0f / det;

    return {
        Vec3{a00 * inv_det, a01 * inv_det, a02 * inv_det},
        Vec3{a10 * inv_det, a11 * inv_det, a12 * inv_det},
        Vec3{a20 * inv_det, a21 * inv_det, a22 * inv_det}
    };
    // clang-format on
}

} // namespace math