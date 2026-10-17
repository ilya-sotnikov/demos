#pragma once

#include "math_common.hpp"
#include "vec3.hpp"

inline f32 Quat::w() const {
    return val[0];
}

inline f32 Quat::x() const {
    return val[1];
}

inline f32 Quat::y() const {
    return val[2];
}

inline f32 Quat::z() const {
    return val[3];
}

inline f32& Quat::w() {
    return val[0];
}

inline f32& Quat::x() {
    return val[1];
}

inline f32& Quat::y() {
    return val[2];
}

inline f32& Quat::z() {
    return val[3];
}

inline Quat Quat::from_axis(f32 rad, f32 x, f32 y, f32 z) {
    Quat res;

    const f32 s = sinf(rad / 2.0f);
    const f32 c = cosf(rad / 2.0f);
    res.val[0] = c;
    res.val[1] = s * x;
    res.val[2] = s * y;
    res.val[3] = s * z;

    return res;
}

inline Quat Quat::from_axis(f32 rad, Vec3 axis) {
    Quat res;

    const f32 s = sinf(rad / 2.0f);
    const f32 c = cosf(rad / 2.0f);
    res.val[0] = c;
    res.val[1] = s * axis.x();
    res.val[2] = s * axis.y();
    res.val[3] = s * axis.z();

    return res;
}

inline f32 Quat::operator[](int i) const {
    DEBUG_ASSERT(i > 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline f32& Quat::operator[](int i) {
    DEBUG_ASSERT(i > 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline Quat operator*(Quat lhs, Quat rhs) {
    // clang-format off
    return {
        lhs.val[0] * rhs.val[0] - lhs.val[1] * rhs.val[1] -
        lhs.val[2] * rhs.val[2] - lhs.val[3] * rhs.val[3],

        lhs.val[0] * rhs.val[1] + lhs.val[1] * rhs.val[0] +
        lhs.val[2] * rhs.val[3] - lhs.val[3] * rhs.val[2],

        lhs.val[0] * rhs.val[2] - lhs.val[1] * rhs.val[3] +
        lhs.val[2] * rhs.val[0] + lhs.val[3] * rhs.val[1],

        lhs.val[0] * rhs.val[3] + lhs.val[1] * rhs.val[2] -
        lhs.val[2] * rhs.val[1] + lhs.val[3] * rhs.val[0]
    };
    // clang-format on
}

inline Quat operator*(Quat lhs, Vec3 rhs) {
    // clang-format off
    return {
        -lhs.val[1] * rhs.val[0] - lhs.val[2] * rhs.val[1] - lhs.val[3] * rhs.val[2],
         lhs.val[0] * rhs.val[0] + lhs.val[2] * rhs.val[2] - lhs.val[3] * rhs.val[1],
         lhs.val[0] * rhs.val[1] - lhs.val[1] * rhs.val[2] + lhs.val[3] * rhs.val[0],
         lhs.val[0] * rhs.val[2] + lhs.val[1] * rhs.val[1] - lhs.val[2] * rhs.val[0]
    };
    // clang-format on
}

inline Quat operator*(Vec3 lhs, Quat rhs) {
    // clang-format off
    return {
    -lhs.val[0] * rhs.val[1] - lhs.val[1] * rhs.val[2] - lhs.val[2] * rhs.val[3],
     lhs.val[0] * rhs.val[0] + lhs.val[1] * rhs.val[3] - lhs.val[2] * rhs.val[2],
    -lhs.val[0] * rhs.val[3] + lhs.val[1] * rhs.val[0] + lhs.val[2] * rhs.val[1],
     lhs.val[0] * rhs.val[2] - lhs.val[1] * rhs.val[1] + lhs.val[2] * rhs.val[0]
    };
    // clang-format on
}

inline bool operator==(Quat lhs, Quat rhs) {
    return (lhs.val[0] == rhs.val[0]) && (lhs.val[1] == rhs.val[1]) && (lhs.val[2] == rhs.val[2])
        && (lhs.val[3] == rhs.val[3]);
}

inline bool operator!=(Quat lhs, Quat rhs) {
    return (lhs.val[0] != rhs.val[0]) || (lhs.val[1] != rhs.val[1]) || (lhs.val[2] != rhs.val[2])
        || (lhs.val[3] != lhs.val[3]);
}

namespace math {

inline bool almost_equal(Quat lhs, Quat rhs, f32 tolerance = FLT_EPSILON) {
    return almost_equal(lhs.val[0], rhs.val[0], tolerance)
        && almost_equal(lhs.val[1], rhs.val[1], tolerance)
        && almost_equal(lhs.val[2], rhs.val[2], tolerance)
        && almost_equal(lhs.val[3], rhs.val[3], tolerance);
}

inline Quat normalize(Quat q) {
    const f32 mag_sq =
        q.val[0] * q.val[0] + q.val[1] * q.val[1] + q.val[2] * q.val[2] + q.val[3] * q.val[3];
    DEBUG_ASSERT(mag_sq != 0.0f);
    const f32 inv_mag = 1.0f / sqrtf(mag_sq);
    q.val[0] *= inv_mag;
    q.val[1] *= inv_mag;
    q.val[2] *= inv_mag;
    q.val[3] *= inv_mag;
    return q;
}

inline Quat conjugate(Quat q) {
    return {q.val[0], -q.val[1], -q.val[2], -q.val[3]};
}

inline Vec3 rotate(Quat lhs, Vec3 rhs) {
    const Vec3 v = {lhs.val[1], lhs.val[2], lhs.val[3]};
    const Vec3 uv = cross(v, rhs);
    const Vec3 uuv = cross(v, uv);

    return rhs + (uuv + uv * lhs.val[0]) * 2.0f;
}

inline Mat3 to_mat3(Quat quat) {
    const f32 w = quat.val[0];
    const f32 x = quat.val[1];
    const f32 y = quat.val[2];
    const f32 z = quat.val[3];
    return Mat3{
        Vec3{
            1.0f - 2.0f * y * y - 2.0f * z * z,
            2.0f * x * y + 2.0f * w * z,
            2.0f * x * z - 2.0f * w * y
        },
        Vec3{
            2.0f * x * y - 2 * w * z,
            1.0f - 2.0f * x * x - 2.0f * z * z,
            2.0f * y * z + 2.0f * w * x
        },
        Vec3{
            2.0f * x * z + 2.0f * w * y,
            2.0f * y * z - 2.0f * w * x,
            1.0f - 2.0f * x * x - 2.0f * y * y
        },
    };
}

inline Mat4 to_mat4(Quat quat) {
    const f32 w = quat.val[0];
    const f32 x = quat.val[1];
    const f32 y = quat.val[2];
    const f32 z = quat.val[3];
    return Mat4{
        Vec4{
            1.0f - 2.0f * y * y - 2.0f * z * z,
            2.0f * x * y + 2.0f * w * z,
            2.0f * x * z - 2.0f * w * y,
            0.0f
        },
        Vec4{
            2.0f * x * y - 2 * w * z,
            1.0f - 2.0f * x * x - 2.0f * z * z,
            2.0f * y * z + 2.0f * w * x,
            0.0f
        },
        Vec4{
            2.0f * x * z + 2.0f * w * y,
            2.0f * y * z - 2.0f * w * x,
            1.0f - 2.0f * x * x - 2.0f * y * y,
            0.0f
        },
        Vec4{0.0f, 0.0f, 0.0f, 1.0f}
    };
}

inline Quat to_quat(Vec3 v) {
    return {0.0f, v.val[0], v.val[1], v.val[2]};
}

inline Vec3 to_vec3(Quat q) {
    return {q.val[1], q.val[2], q.val[3]};
}

inline void clear(Quat& q) {
    q.val[0] = 0.0f;
    q.val[1] = 0.0f;
    q.val[2] = 0.0f;
    q.val[3] = 0.0f;
}

} // namespace math