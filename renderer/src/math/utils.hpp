#pragma once

#include "common.hpp"
#include "types.hpp"

#include <float.h>
#include <math.h>

namespace math {

// returns static data, not thread-safe
const char* to_string(Vec2 v);
const char* to_string(Vec3 v);
const char* to_string(Vec4 v);
const char* to_string(Mat2 m);
const char* to_string(Mat3 m);
const char* to_string(Mat4 m);
const char* to_string(Quat q);

void print(Vec2 v);
void print(Vec3 v);
void print(Vec4 v);
void print(Mat2 m);
void print(Mat3 m);
void print(Mat4 m);
void print(Quat q);

// xorshift LFSR, initial value should be != 0
u32 lfsr_next(u32 value);
f32 lfsr_next_get_float_abs(u32& value, f32 amplitude);
f32 lfsr_next_get_float(u32& value, f32 amplitude);

inline f32 abs(f32 x) {
    return fabsf(x);
}

inline f64 abs(f64 x) {
    return fabs(x);
}

inline f32 sign(f32 x) {
    return x < 0.0f ? -1.0f : 1.0f;
}

inline constexpr f32 degrees(f32 radians) {
    return 180.0f * radians / M_PIf;
}

inline constexpr f32 radians(f32 degrees) {
    return M_PIf * degrees / 180.0f;
}

template<typename T>
T min(T a, T b) {
    return a < b ? a : b;
}

inline Vec2 min(Vec2 a, Vec2 b) {
    const f32 x = a.val[0] < b.val[0] ? a.val[0] : b.val[0];
    const f32 y = a.val[1] < b.val[1] ? a.val[1] : b.val[1];
    return {x, y};
}

inline Vec3 min(Vec3 a, Vec3 b) {
    const f32 x = a.val[0] < b.val[0] ? a.val[0] : b.val[0];
    const f32 y = a.val[1] < b.val[1] ? a.val[1] : b.val[1];
    const f32 z = a.val[2] < b.val[2] ? a.val[2] : b.val[2];
    return {x, y, z};
}

inline Vec4 min(Vec4 a, Vec4 b) {
    const f32 x = a.val[0] < b.val[0] ? a.val[0] : b.val[0];
    const f32 y = a.val[1] < b.val[1] ? a.val[1] : b.val[1];
    const f32 z = a.val[2] < b.val[2] ? a.val[2] : b.val[2];
    const f32 w = a.val[3] < b.val[3] ? a.val[3] : b.val[3];
    return {x, y, z, w};
}

template<typename T>
T max(T a, T b) {
    return a > b ? a : b;
}

inline Vec2 max(Vec2 a, Vec2 b) {
    const f32 x = a.val[0] > b.val[0] ? a.val[0] : b.val[0];
    const f32 y = a.val[1] > b.val[1] ? a.val[1] : b.val[1];
    return {x, y};
}

inline Vec3 max(Vec3 a, Vec3 b) {
    const f32 x = a.val[0] > b.val[0] ? a.val[0] : b.val[0];
    const f32 y = a.val[1] > b.val[1] ? a.val[1] : b.val[1];
    const f32 z = a.val[2] > b.val[2] ? a.val[2] : b.val[2];
    return {x, y, z};
}

inline Vec4 max(Vec4 a, Vec4 b) {
    const f32 x = a.val[0] > b.val[0] ? a.val[0] : b.val[0];
    const f32 y = a.val[1] > b.val[1] ? a.val[1] : b.val[1];
    const f32 z = a.val[2] > b.val[2] ? a.val[2] : b.val[2];
    const f32 w = a.val[3] > b.val[3] ? a.val[3] : b.val[3];
    return {x, y, z, w};
}

template<typename T>
T max(T a, T b, T c) {
    T result = a;
    if (b > result) {
        result = b;
    }
    if (c > result) {
        result = c;
    }
    return result;
}

template<typename Float>
inline bool almost_equal(Float a, Float b, Float absolute_tolerance, Float relative_tolerance) {
    return abs(a - b) <= max(absolute_tolerance, relative_tolerance * max(Abs(a), Abs(b)));
}

// When absolute_tolerance == relative_tolerance.
template<typename Float>
inline bool almost_equal(Float a, Float b, Float tolerance = FLT_EPSILON) {
    return abs(a - b) <= tolerance * max(Float(1), abs(a), abs(b));
}

// Not robust but fuck it.
inline f32 fract(f32 x) {
    return x - f32(i64(x));
}

template<typename T>
T clamp(T x, T min, T max) {
    return x > max ? max : x < min ? min : x;
}

template<typename T>
void swap(T& a, T& b) {
    const T tmp = a;
    a = b;
    b = tmp;
}

inline f32 square(f32 x) {
    return x * x;
}

// https://en.wikipedia.org/wiki/Halton_sequence
inline f32 halton_sequence(u32 idx, u32 base) {
    DEBUG_ASSERT(idx > 0);

    f32 f = 1.0f;
    f32 r = 0.0f;

    while (idx > 0) {
        f /= f32(base);
        r = r + f * f32(idx % base);
        idx = u32(floorf(f32(idx) / f32(base)));
    }

    return r;
}

inline u32 previous_pow2(u32 x) {
    u32 result = 1;

    while (result * 2 < x) {
        result *= 2;
    }

    return result;
}

inline Vec3 exp(Vec3 v) {
    return {
        expf(v.val[0]),
        expf(v.val[1]),
        expf(v.val[2]),
    };
}

} // namespace math