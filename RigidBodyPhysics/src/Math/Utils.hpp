#pragma once

#include "../Common.hpp"
#include "Types.hpp"

#include <math.h>
#include <float.h>

// returns static data, not thread-safe
[[nodiscard]]
const char* ToString(Vec2 v);
[[nodiscard]]
const char* ToString(Vec3 v);
[[nodiscard]]
const char* ToString(Vec4 v);
[[nodiscard]]
const char* ToString(Mat2 m);
[[nodiscard]]
const char* ToString(Mat3 m);
[[nodiscard]]
const char* ToString(Mat4 m);
[[nodiscard]]
const char* ToString(Mat4 q);

void Print(Vec2 v);
void Print(Vec3 v);
void Print(Vec4 v);
void Print(Mat2 m);
void Print(Mat3 m);
void Print(Mat4 m);
void Print(Quat q);

// xorshift LFSR, initial value should be != 0
[[nodiscard]]
u32 LfsrNext(u32 value);
[[nodiscard]]
f32 LfsrNextGetFloatAbs(u32& value, f32 amplitude);
[[nodiscard]]
f32 LfsrNextGetFloat(u32& value, f32 amplitude);

[[nodiscard]]
inline f32 Abs(f32 x)
{
    return fabsf(x);
}

[[nodiscard]]
inline f64 Abs(f64 x)
{
    return fabs(x);
}

[[nodiscard]]
inline f32 Sign(f32 x)
{
    return x < 0.0f ? -1.0f : 1.0f;
}

[[nodiscard]]
inline f32 Degrees(f32 radians)
{
    return 180.0f * radians / M_PIf;
}

[[nodiscard]]
constexpr inline f32 Radians(f32 degrees)
{
    return M_PIf * degrees / 180.0f;
}

template <typename T>
[[nodiscard]]
T Min(T a, T b)
{
    return a < b ? a : b;
}

inline Vec2 Min(Vec2 a, Vec2 b)
{
    const f32 x = a.mVal[0] < b.mVal[0] ? a.mVal[0] : b.mVal[0];
    const f32 y = a.mVal[1] < b.mVal[1] ? a.mVal[1] : b.mVal[1];
    return {x, y};
}

inline Vec3 Min(Vec3 a, Vec3 b)
{
    const f32 x = a.mVal[0] < b.mVal[0] ? a.mVal[0] : b.mVal[0];
    const f32 y = a.mVal[1] < b.mVal[1] ? a.mVal[1] : b.mVal[1];
    const f32 z = a.mVal[2] < b.mVal[2] ? a.mVal[2] : b.mVal[2];
    return {x, y, z};
}

inline Vec4 Min(Vec4 a, Vec4 b)
{
    const f32 x = a.mVal[0] < b.mVal[0] ? a.mVal[0] : b.mVal[0];
    const f32 y = a.mVal[1] < b.mVal[1] ? a.mVal[1] : b.mVal[1];
    const f32 z = a.mVal[2] < b.mVal[2] ? a.mVal[2] : b.mVal[2];
    const f32 w = a.mVal[3] < b.mVal[3] ? a.mVal[3] : b.mVal[3];
    return {x, y, z, w};
}

template <typename T>
[[nodiscard]]
T Max(T a, T b)
{
    return a > b ? a : b;
}

inline Vec2 Max(Vec2 a, Vec2 b)
{
    const f32 x = a.mVal[0] > b.mVal[0] ? a.mVal[0] : b.mVal[0];
    const f32 y = a.mVal[1] > b.mVal[1] ? a.mVal[1] : b.mVal[1];
    return {x, y};
}

inline Vec3 Max(Vec3 a, Vec3 b)
{
    const f32 x = a.mVal[0] > b.mVal[0] ? a.mVal[0] : b.mVal[0];
    const f32 y = a.mVal[1] > b.mVal[1] ? a.mVal[1] : b.mVal[1];
    const f32 z = a.mVal[2] > b.mVal[2] ? a.mVal[2] : b.mVal[2];
    return {x, y, z};
}

inline Vec4 Max(Vec4 a, Vec4 b)
{
    const f32 x = a.mVal[0] > b.mVal[0] ? a.mVal[0] : b.mVal[0];
    const f32 y = a.mVal[1] > b.mVal[1] ? a.mVal[1] : b.mVal[1];
    const f32 z = a.mVal[2] > b.mVal[2] ? a.mVal[2] : b.mVal[2];
    const f32 w = a.mVal[3] > b.mVal[3] ? a.mVal[3] : b.mVal[3];
    return {x, y, z, w};
}

template <typename T>
[[nodiscard]]
T Max(T a, T b, T c)
{
    T result = a;
    if (b > result)
    {
        result = b;
    }
    if (c > result)
    {
        result = c;
    }
    return result;
}

template <typename Float>
[[nodiscard]]
inline constexpr bool AlmostEqual(
    Float a,
    Float b,
    Float absoluteTolerance,
    Float relativeTolerance
)
{
    return Abs(a - b) <= Max(absoluteTolerance, relativeTolerance * Max(Abs(a), Abs(b)));
}

// When absoluteTolerance == relativeTolerance.
template <typename Float>
[[nodiscard]]
inline constexpr bool AlmostEqual(Float a, Float b, Float tolerance = FLT_EPSILON)
{
    return Abs(a - b) <= tolerance * Max(static_cast<Float>(1), Abs(a), Abs(b));
}

// Not robust but fuck it.
[[nodiscard]]
inline constexpr f32 Fract(f32 x)
{
    return x - static_cast<f32>(static_cast<long long>(x));
}

template <typename T>
[[nodiscard]]
constexpr T Clamp(T x, T min, T max)
{
    return x > max ? max : x < min ? min : x;
}

template <typename T>
constexpr void Swap(T& a, T& b)
{
    const T tmp = a;
    a = b;
    b = tmp;
}

inline constexpr f32 Square(f32 x)
{
    return x * x;
}
