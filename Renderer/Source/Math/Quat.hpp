#pragma once

#include "MathCommon.hpp"
#include "Vec3.hpp"

inline constexpr f32 Quat::W() const
{
    return val[0];
}

inline constexpr f32 Quat::X() const
{
    return val[1];
}

inline constexpr f32 Quat::Y() const
{
    return val[2];
}

inline constexpr f32 Quat::Z() const
{
    return val[3];
}

inline constexpr f32& Quat::W()
{
    return val[0];
}

inline constexpr f32& Quat::X()
{
    return val[1];
}

inline constexpr f32& Quat::Y()
{
    return val[2];
}

inline constexpr f32& Quat::Z()
{
    return val[3];
}

// TODO: constexpr after reimplementing trigonometry
// (reimplementing them will also help with cross-compiler determinism).
inline Quat Quat::FromAxis(f32 rad, f32 x, f32 y, f32 z)
{
    Quat res;

    const f32 s = sinf(rad / 2.0f);
    const f32 c = cosf(rad / 2.0f);
    res.val[0] = c;
    res.val[1] = s * x;
    res.val[2] = s * y;
    res.val[3] = s * z;

    return res;
}

inline Quat Quat::FromAxis(f32 rad, Vec3 axis)
{
    Quat res;

    const f32 s = sinf(rad / 2.0f);
    const f32 c = cosf(rad / 2.0f);
    res.val[0] = c;
    res.val[1] = s * axis.X();
    res.val[2] = s * axis.Y();
    res.val[3] = s * axis.Z();

    return res;
}

inline constexpr f32 Quat::operator[](int i) const
{
    DEBUG_ASSERT(i > 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline constexpr f32& Quat::operator[](int i)
{
    DEBUG_ASSERT(i > 0);
    DEBUG_ASSERT(i < N);
    return val[i];
}

inline constexpr Quat operator*(Quat lhs, Quat rhs)
{
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

inline constexpr Quat operator*(Quat lhs, Vec3 rhs)
{
    // clang-format off
    return {
        -lhs.val[1] * rhs.val[0] - lhs.val[2] * rhs.val[1] - lhs.val[3] * rhs.val[2],
         lhs.val[0] * rhs.val[0] + lhs.val[2] * rhs.val[2] - lhs.val[3] * rhs.val[1],
         lhs.val[0] * rhs.val[1] - lhs.val[1] * rhs.val[2] + lhs.val[3] * rhs.val[0],
         lhs.val[0] * rhs.val[2] + lhs.val[1] * rhs.val[1] - lhs.val[2] * rhs.val[0]
    };
    // clang-format on
}

inline constexpr Quat operator*(Vec3 lhs, Quat rhs)
{
    // clang-format off
    return {
    -lhs.val[0] * rhs.val[1] - lhs.val[1] * rhs.val[2] - lhs.val[2] * rhs.val[3],
     lhs.val[0] * rhs.val[0] + lhs.val[1] * rhs.val[3] - lhs.val[2] * rhs.val[2],
    -lhs.val[0] * rhs.val[3] + lhs.val[1] * rhs.val[0] + lhs.val[2] * rhs.val[1],
     lhs.val[0] * rhs.val[2] - lhs.val[1] * rhs.val[1] + lhs.val[2] * rhs.val[0]
    };
    // clang-format on
}

[[nodiscard]]
inline constexpr bool operator==(Quat lhs, Quat rhs)
{
    return (lhs.val[0] == rhs.val[0]) && (lhs.val[1] == rhs.val[1]) && (lhs.val[2] == rhs.val[2])
        && (lhs.val[3] == rhs.val[3]);
}

[[nodiscard]]
inline constexpr bool operator!=(Quat lhs, Quat rhs)
{
    return (lhs.val[0] != rhs.val[0]) || (lhs.val[1] != rhs.val[1]) || (lhs.val[2] != rhs.val[2])
        || (lhs.val[3] != lhs.val[3]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Quat lhs, Quat rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.val[0], rhs.val[0], tolerance)
        && AlmostEqual(lhs.val[1], rhs.val[1], tolerance)
        && AlmostEqual(lhs.val[2], rhs.val[2], tolerance)
        && AlmostEqual(lhs.val[3], rhs.val[3], tolerance);
}

inline constexpr Quat Normalize(Quat q)
{
    const f32 magSq
        = q.val[0] * q.val[0] + q.val[1] * q.val[1] + q.val[2] * q.val[2] + q.val[3] * q.val[3];
    DEBUG_ASSERT(magSq != 0.0f);
    const f32 invMag = 1.0f / sqrtf(magSq);
    q.val[0] *= invMag;
    q.val[1] *= invMag;
    q.val[2] *= invMag;
    q.val[3] *= invMag;
    return q;
}

inline constexpr Quat Conjugate(Quat q)
{
    return {q.val[0], -q.val[1], -q.val[2], -q.val[3]};
}

inline constexpr Vec3 Rotate(Quat lhs, Vec3 rhs)
{
    const Vec3 v = {lhs.val[1], lhs.val[2], lhs.val[3]};
    const Vec3 uv = Cross(v, rhs);
    const Vec3 uuv = Cross(v, uv);

    return rhs + (uuv + uv * lhs.val[0]) * 2.0f;
}

inline constexpr Mat3 ToMat3(Quat quat)
{
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

inline constexpr Mat4 ToMat4(Quat quat)
{
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

inline constexpr Quat ToQuat(Vec3 v)
{
    return {0.0f, v.val[0], v.val[1], v.val[2]};
}

inline constexpr Vec3 ToVec3(Quat q)
{
    return {q.val[1], q.val[2], q.val[3]};
}

inline constexpr void Clear(Quat& q)
{
    q.val[0] = 0.0f;
    q.val[1] = 0.0f;
    q.val[2] = 0.0f;
    q.val[3] = 0.0f;
}
