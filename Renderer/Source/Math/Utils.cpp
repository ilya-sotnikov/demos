#include "Utils.hpp"

#include <stdio.h>

static char sStringBuffer[1024];

const char* ToString(Vec2 v)
{
    snprintf(sStringBuffer, sizeof(sStringBuffer), "%f %f", v.val[0], v.val[1]);
    return sStringBuffer;
}

const char* ToString(Vec3 v)
{
    // clang-format off
    snprintf(
        sStringBuffer,
        sizeof(sStringBuffer),
        "%f %f %f",
        v.val[0], v.val[1], v.val[2]
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
        v.val[0],
        v.val[1],
        v.val[2],
        v.val[3]
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
        m.col[0].val[0], m.col[1].val[0],
        m.col[0].val[1], m.col[1].val[1]
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
        m.col[0].val[0], m.col[1].val[0], m.col[2].val[0],
        m.col[0].val[1], m.col[1].val[1], m.col[2].val[1],
        m.col[0].val[2], m.col[1].val[2], m.col[2].val[2]
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
        m.col[0].val[0], m.col[1].val[0], m.col[2].val[0], m.col[3].val[0],
        m.col[0].val[1], m.col[1].val[1], m.col[2].val[1], m.col[3].val[1],
        m.col[0].val[2], m.col[1].val[2], m.col[2].val[2], m.col[3].val[2],
        m.col[0].val[3], m.col[1].val[3], m.col[2].val[3], m.col[3].val[3]
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
        q.val[0], q.val[1], q.val[2], q.val[3]
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
    return (f32(value) / f32(UINT32_MAX)) * amplitude;
}

f32 LfsrNextGetFloat(u32& value, f32 amplitude)
{
    value = LfsrNext(value);
    return (f32(value) / f32(UINT32_MAX / 2U) - 1.0f) * amplitude;
}
