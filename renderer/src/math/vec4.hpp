#pragma once

#include "math_common.hpp"

inline f32 Vec4::x() const {
    return val[0];
}

inline f32 Vec4::y() const {
    return val[1];
}

inline f32 Vec4::z() const {
    return val[2];
}

inline f32 Vec4::w() const {
    return val[3];
}

inline f32 Vec4::r() const {
    return val[0];
}

inline f32 Vec4::g() const {
    return val[1];
}

inline f32 Vec4::b() const {
    return val[2];
}

inline f32 Vec4::a() const {
    return val[3];
}

inline f32& Vec4::x() {
    return val[0];
}

inline f32& Vec4::y() {
    return val[1];
}

inline f32& Vec4::z() {
    return val[2];
}

inline f32& Vec4::w() {
    return val[3];
}

inline f32& Vec4::r() {
    return val[0];
}

inline f32& Vec4::g() {
    return val[1];
}

inline f32& Vec4::b() {
    return val[2];
}

inline f32& Vec4::a() {
    return val[3];
}

inline Vec2 Vec4::xy() const {
    return {val[0], val[1]};
}

inline Vec3 Vec4::xyz() const {
    return {val[0], val[1], val[2]};
}

inline Vec3 Vec4::rgb() const {
    return {val[0], val[1], val[2]};
}

inline f32 Vec4::operator[](int i) const {
    DEBUG_ASSERT(i >= 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline f32& Vec4::operator[](int i) {
    DEBUG_ASSERT(i >= 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline Vec4 operator-(Vec4 v) {
    return {-v.val[0], -v.val[1], -v.val[2], -v.val[3]};
}

inline Vec4& operator+=(Vec4& lhs, Vec4 rhs) {
    lhs.val[0] += rhs.val[0];
    lhs.val[1] += rhs.val[1];
    lhs.val[2] += rhs.val[2];
    lhs.val[3] += rhs.val[3];
    return lhs;
}

inline Vec4& operator-=(Vec4& lhs, Vec4 rhs) {
    lhs.val[0] -= rhs.val[0];
    lhs.val[1] -= rhs.val[1];
    lhs.val[2] -= rhs.val[2];
    lhs.val[3] -= rhs.val[3];
    return lhs;
}

inline Vec4& operator*=(Vec4& lhs, Vec4 rhs) {
    lhs.val[0] *= rhs.val[0];
    lhs.val[1] *= rhs.val[1];
    lhs.val[2] *= rhs.val[2];
    lhs.val[3] *= rhs.val[3];
    return lhs;
}

inline Vec4& operator*=(Vec4& lhs, f32 rhs) {
    lhs.val[0] *= rhs;
    lhs.val[1] *= rhs;
    lhs.val[2] *= rhs;
    lhs.val[3] *= rhs;
    return lhs;
}

inline Vec4& operator/=(Vec4& lhs, f32 rhs) {
    lhs.val[0] /= rhs;
    lhs.val[1] /= rhs;
    lhs.val[2] /= rhs;
    lhs.val[3] /= rhs;
    return lhs;
}

inline Vec4& operator/=(Vec4& lhs, Vec4 rhs) {
    lhs.val[0] /= rhs.val[0];
    lhs.val[1] /= rhs.val[1];
    lhs.val[2] /= rhs.val[2];
    lhs.val[3] /= rhs.val[3];
    return lhs;
}

inline Vec4 operator+(Vec4 lhs, Vec4 rhs) {
    return {
        lhs.val[0] + rhs.val[0],
        lhs.val[1] + rhs.val[1],
        lhs.val[2] + rhs.val[2],
        lhs.val[3] + rhs.val[3],
    };
}

inline Vec4 operator-(Vec4 lhs, Vec4 rhs) {
    return {
        lhs.val[0] - rhs.val[0],
        lhs.val[1] - rhs.val[1],
        lhs.val[2] - rhs.val[2],
        lhs.val[3] - rhs.val[3],
    };
}

inline Vec4 operator*(Vec4 lhs, Vec4 rhs) {
    return {
        lhs.val[0] * rhs.val[0],
        lhs.val[1] * rhs.val[1],
        lhs.val[2] * rhs.val[2],
        lhs.val[3] * rhs.val[3],
    };
}

inline Vec4 operator*(Vec4 lhs, f32 rhs) {
    return {
        lhs.val[0] * rhs,
        lhs.val[1] * rhs,
        lhs.val[2] * rhs,
        lhs.val[3] * rhs,
    };
}

inline Vec4 operator/(Vec4 lhs, f32 rhs) {
    return {
        lhs.val[0] / rhs,
        lhs.val[1] / rhs,
        lhs.val[2] / rhs,
        lhs.val[3] / rhs,
    };
}

inline Vec4 operator/(Vec4 lhs, Vec4 rhs) {
    return {
        lhs.val[0] / rhs.val[0],
        lhs.val[1] / rhs.val[1],
        lhs.val[2] / rhs.val[2],
        lhs.val[3] / rhs.val[3],
    };
}

inline Vec4 operator*(f32 lhs, Vec4 rhs) {
    return {
        lhs * rhs.val[0],
        lhs * rhs.val[1],
        lhs * rhs.val[2],
        lhs * rhs.val[3],
    };
}

inline bool operator==(Vec4 lhs, Vec4 rhs) {
    return (lhs.val[0] == rhs.val[0]) && (lhs.val[1] == rhs.val[1]) && (lhs.val[2] == rhs.val[2])
        && (lhs.val[3] == rhs.val[3]);
}

inline bool operator!=(Vec4 lhs, Vec4 rhs) {
    return (lhs.val[0] != rhs.val[0]) || (lhs.val[1] != rhs.val[1]) || (lhs.val[2] != rhs.val[2])
        || (lhs.val[3] != lhs.val[3]);
}

namespace math {

inline bool almost_equal(Vec4 lhs, Vec4 rhs, f32 tolerance = FLT_EPSILON) {
    return almost_equal(lhs.val[0], rhs.val[0], tolerance)
        && almost_equal(lhs.val[1], rhs.val[1], tolerance)
        && almost_equal(lhs.val[2], rhs.val[2], tolerance)
        && almost_equal(lhs.val[3], rhs.val[3], tolerance);
}

inline Vec4 abs(Vec4 v) {
    return {fabsf(v.val[0]), fabsf(v.val[1]), fabsf(v.val[2]), fabsf(v.val[3])};
}

inline f32 dot(Vec4 a, Vec4 b) {
    return a.val[0] * b.val[0] + a.val[1] * b.val[1] + a.val[2] * b.val[2] + a.val[3] * b.val[3];
}

inline f32 magnitude_sq(Vec4 v) {
    return v.val[0] * v.val[0] + v.val[1] * v.val[1] + v.val[2] * v.val[2] + v.val[3] * v.val[3];
}

inline f32 magnitude(Vec4 v) {
    return sqrtf(
        v.val[0] * v.val[0] + v.val[1] * v.val[1] + v.val[2] * v.val[2] + v.val[3] * v.val[3]
    );
}

inline Vec4 normalize(Vec4 v) {
    const f32 mag = sqrtf(
        v.val[0] * v.val[0] + v.val[1] * v.val[1] + v.val[2] * v.val[2] + v.val[3] * v.val[3]
    );
    DEBUG_ASSERT(mag != 0.0f);
    return {v.val[0] / mag, v.val[1] / mag, v.val[2] / mag, v.val[3] / mag};
}

inline Vec4 normalize_plane(Vec4 p) {
    const f32 mag = sqrtf(p.val[0] * p.val[0] + p.val[1] * p.val[1] + p.val[2] * p.val[2]);
    DEBUG_ASSERT(mag != 0.0f);
    return {p.val[0] / mag, p.val[1] / mag, p.val[2] / mag, p.val[3] / mag};
}

inline void clear(Vec4& v) {
    v.val[0] = 0.0f;
    v.val[1] = 0.0f;
    v.val[2] = 0.0f;
    v.val[3] = 0.0f;
}

} // namespace math