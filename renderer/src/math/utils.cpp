#include "utils.hpp"

#include <stdio.h>

static char s_string_buffer[1024];

const char* math::to_string(Vec2 v) {
    snprintf(s_string_buffer, sizeof(s_string_buffer), "%f %f", v.val[0], v.val[1]);
    return s_string_buffer;
}

const char* math::to_string(Vec3 v) {
    // clang-format off
    snprintf(
        s_string_buffer,
        sizeof(s_string_buffer),
        "%f %f %f",
        v.val[0], v.val[1], v.val[2]
    );
    // clang-format on
    return s_string_buffer;
}

const char* math::to_string(Vec4 v) {
    snprintf(
        s_string_buffer,
        sizeof(s_string_buffer),
        "%f %f %f %f",
        v.val[0],
        v.val[1],
        v.val[2],
        v.val[3]
    );
    return s_string_buffer;
}

const char* math::to_string(Mat2 m) {
    // clang-format off
    snprintf(
        s_string_buffer,
        sizeof(s_string_buffer),
        "%f %f\n"
        "%f %f",
        m.col[0].val[0], m.col[1].val[0],
        m.col[0].val[1], m.col[1].val[1]
    );
    // clang-format on
    return s_string_buffer;
}

const char* math::to_string(Mat3 m) {
    // clang-format off
    snprintf(
        s_string_buffer,
        sizeof(s_string_buffer),
        "%f %f %f\n"
        "%f %f %f\n"
        "%f %f %f",
        m.col[0].val[0], m.col[1].val[0], m.col[2].val[0],
        m.col[0].val[1], m.col[1].val[1], m.col[2].val[1],
        m.col[0].val[2], m.col[1].val[2], m.col[2].val[2]
    );
    // clang-format on
    return s_string_buffer;
}

const char* math::to_string(Mat4 m) {
    // clang-format off
    snprintf(
        s_string_buffer,
        sizeof(s_string_buffer),
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
    return s_string_buffer;
}

const char* math::to_string(Quat q) {
    // clang-format off
    snprintf(
        s_string_buffer,
        sizeof(s_string_buffer),
        "%f %f %f %f",
        q.val[0], q.val[1], q.val[2], q.val[3]
    );
    // clang-format on
    return s_string_buffer;
}

void math::print(Vec2 v) {
    puts(to_string(v));
}

void math::print(Vec3 v) {
    puts(to_string(v));
}

void math::print(Vec4 v) {
    puts(to_string(v));
}

void math::print(Mat2 m) {
    puts(to_string(m));
}

void math::print(Mat3 m) {
    puts(to_string(m));
}

void math::print(Mat4 m) {
    puts(to_string(m));
}

void math::print(Quat q) {
    puts(to_string(q));
}

// xorshift LFSR, initial value should be != 0
u32 math::lfsr_next(u32 value) {
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

f32 math::lfsr_next_get_float_abs(u32& value, f32 amplitude) {
    value = lfsr_next(value);
    return (f32(value) / f32(UINT32_MAX)) * amplitude;
}

f32 math::lfsr_next_get_float(u32& value, f32 amplitude) {
    value = lfsr_next(value);
    return (f32(value) / f32(UINT32_MAX / 2U) - 1.0f) * amplitude;
}
