#include "Utils.hpp"

#include <stdio.h>

static char sStringBuffer[1024];

const char* ToString(Vec2 v)
{
    snprintf(sStringBuffer, sizeof(sStringBuffer), "%f %f", v.mVal[0], v.mVal[1]);
    return sStringBuffer;
}

const char* ToString(Vec3 v)
{
    // clang-format off
    snprintf(
        sStringBuffer,
        sizeof(sStringBuffer),
        "%f %f %f",
        v.mVal[0], v.mVal[1], v.mVal[2]
    );
    // clang-format on
    return sStringBuffer;
}

const char* ToString(Vec4 v)
{
    snprintf(
        sStringBuffer,
        sizeof(sStringBuffer),
        "%f %f %f %f",
        v.mVal[0],
        v.mVal[1],
        v.mVal[2],
        v.mVal[3]
    );
    return sStringBuffer;
}

const char* ToString(Mat2 m)
{
    // clang-format off
    snprintf(
        sStringBuffer,
        sizeof(sStringBuffer),
        "%f %f\n"
        "%f %f",
        m.mCol[0].mVal[0], m.mCol[1].mVal[0],
        m.mCol[0].mVal[1], m.mCol[1].mVal[1]
    );
    // clang-format on
    return sStringBuffer;
}

const char* ToString(Mat3 m)
{
    // clang-format off
    snprintf(
        sStringBuffer,
        sizeof(sStringBuffer),
        "%f %f %f\n"
        "%f %f %f\n"
        "%f %f %f",
        m.mCol[0].mVal[0], m.mCol[1].mVal[0], m.mCol[2].mVal[0],
        m.mCol[0].mVal[1], m.mCol[1].mVal[1], m.mCol[2].mVal[1],
        m.mCol[0].mVal[2], m.mCol[1].mVal[2], m.mCol[2].mVal[2]
    );
    // clang-format on
    return sStringBuffer;
}

const char* ToString(Mat4 m)
{
    // clang-format off
    snprintf(
        sStringBuffer,
        sizeof(sStringBuffer),
        "%f %f %f %f\n"
        "%f %f %f %f\n"
        "%f %f %f %f\n"
        "%f %f %f %f",
        m.mCol[0].mVal[0], m.mCol[1].mVal[0], m.mCol[2].mVal[0], m.mCol[3].mVal[0],
        m.mCol[0].mVal[1], m.mCol[1].mVal[1], m.mCol[2].mVal[1], m.mCol[3].mVal[1],
        m.mCol[0].mVal[2], m.mCol[1].mVal[2], m.mCol[2].mVal[2], m.mCol[3].mVal[2],
        m.mCol[0].mVal[3], m.mCol[1].mVal[3], m.mCol[2].mVal[3], m.mCol[3].mVal[3]
    );
    // clang-format on
    return sStringBuffer;
}

const char* ToString(Quat q)
{
    // clang-format off
    snprintf(
        sStringBuffer,
        sizeof(sStringBuffer),
        "%f %f %f %f",
        q.mVal[0], q.mVal[1], q.mVal[2], q.mVal[3]
    );
    // clang-format on
    return sStringBuffer;
}

void Print(Vec2 v)
{
    puts(ToString(v));
}

void Print(Vec3 v)
{
    puts(ToString(v));
}

void Print(Vec4 v)
{
    puts(ToString(v));
}

void Print(Mat2 m)
{
    puts(ToString(m));
}

void Print(Mat3 m)
{
    puts(ToString(m));
}

void Print(Mat4 m)
{
    puts(ToString(m));
}

void Print(Quat q)
{
    puts(ToString(q));
}

// xorshift LFSR, initial value should be != 0
u32 LfsrNext(u32 value)
{
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

f32 LfsrNextGetFloatAbs(u32& value, f32 amplitude)
{
    value = LfsrNext(value);
    return (static_cast<f32>(value) / static_cast<f32>(UINT32_MAX)) * amplitude;
}

f32 LfsrNextGetFloat(u32& value, f32 amplitude)
{
    value = LfsrNext(value);
    return (static_cast<f32>(value) / static_cast<f32>(UINT32_MAX / 2U) - 1.0f) * amplitude;
}
