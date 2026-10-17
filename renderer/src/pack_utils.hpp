#pragma once

#include "common.hpp"
#include "math/utils.hpp"
#include "math/vec2.hpp"
#include "math/vec3.hpp"
#include "utils.hpp"

inline f32 pack_to_f32(u8 x, u8 y, u8 z) {
    const u32 packedU32 = (u32(x) << 16) | (u32(y) << 8) | u32(z);
    const f64 packedF64 = f64(packedU32) / (1U << 24);
    return f32(packedF64);
}

inline f32 pack_to_f32(Vec3 value) {
    value *= 255.0f;
    const u8 x = u8(value.x());
    const u8 y = u8(value.y());
    const u8 z = u8(value.z());
    const u32 packedU32 = (u32(x) << 16) | (u32(y) << 8) | u32(z);
    const f64 packedF64 = f64(packedU32) / (1U << 24);
    return f32(packedF64);
}

inline Vec3 unpack_to_vec3(f32 value) {
    Vec3 result{};
    result.r() = math::fract(value);
    result.g() = math::fract(value * 256.0f);
    result.b() = math::fract(value * 65536.0f);
    return result;
}

// https://www.elopezr.com/the-art-of-packing-data/
inline Vec2 pack_normal_octahedral(Vec3 normal) {
    normal /= fabsf(normal.x()) + fabsf(normal.y()) + fabsf(normal.z());
    if (normal.z() < 0.0f) {
        const f32 x = normal.x();
        const f32 y = normal.y();
        normal.x() = (1.0f - fabsf(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        normal.y() = (1.0f - fabsf(x)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    normal.x() = normal.x() * 0.5f + 0.5f;
    normal.y() = normal.y() * 0.5f + 0.5f;
    return {normal.x(), normal.y()};
}

inline Vec3 unpack_normal_octahedral(Vec2 packed) {
    packed = packed * 2.0f - Vec2{1.0f};

    Vec3 n = {packed.x(), packed.y(), 1.0f - fabsf(packed.x()) - fabsf(packed.y())};
    const float t = math::clamp(-n.z(), 0.0f, 1.0f);
    n.x() += n.x() >= 0.0f ? -t : t;
    n.y() += n.y() >= 0.0f ? -t : t;
    return math::normalize(n);
}

inline u16 pack_float2_to_rg8snorm(Vec2 value) {
    DEBUG_ASSERT(value.x() >= -1.0f);
    DEBUG_ASSERT(value.x() <= 1.0f);
    DEBUG_ASSERT(value.y() >= -1.0f);
    DEBUG_ASSERT(value.y() <= 1.0f);

    const u8 byte_x = utils::bit_cast<u8>(i8(roundf(value.x() * 127.0f)));
    const u8 byte_y = utils::bit_cast<u8>(i8(roundf(value.y() * 127.0f)));
    return u16(byte_y << 8) | byte_x;
}

inline Vec2 unpack_rg8snorm_to_float2(u16 packed) {
    const i32 int_x = i32(packed << 24) >> 24;
    const i32 int_y = i32(packed << 16) >> 24;
    return {f32(int_x) / 127.0f, f32(int_y) / 127.0f};
}
