#pragma once

#include "MathCommon.hpp"
#include "Vec3.hpp"

inline constexpr f32 Quat::W() const
{
    return mVal[0];
}

inline constexpr f32 Quat::X() const
{
    return mVal[1];
}

inline constexpr f32 Quat::Y() const
{
    return mVal[2];
}

inline constexpr f32 Quat::Z() const
{
    return mVal[3];
}

inline constexpr f32& Quat::W()
{
    return mVal[0];
}

inline constexpr f32& Quat::X()
{
    return mVal[1];
}

inline constexpr f32& Quat::Y()
{
    return mVal[2];
}

inline constexpr f32& Quat::Z()
{
    return mVal[3];
}

// TODO: constexpr after reimplementing trigonometry
// (reimplementing them will also help with cross-compiler determinism).
inline Quat Quat::FromAxis(f32 rad, f32 x, f32 y, f32 z)
{
    Quat res;

    const f32 s = sinf(rad / 2.0f);
    const f32 c = cosf(rad / 2.0f);
    res.mVal[0] = c;
    res.mVal[1] = s * x;
    res.mVal[2] = s * y;
    res.mVal[3] = s * z;

    return res;
}

inline Quat Quat::FromAxis(f32 rad, Vec3 axis)
{
    Quat res;

    const f32 s = sinf(rad / 2.0f);
    const f32 c = cosf(rad / 2.0f);
    res.mVal[0] = c;
    res.mVal[1] = s * axis.X();
    res.mVal[2] = s * axis.Y();
    res.mVal[3] = s * axis.Z();

    return res;
}

inline constexpr f32 Quat::operator[](int i) const
{
    assert(i > 0);
    assert(i < N);
    return mVal[i];
}

inline constexpr f32& Quat::operator[](int i)
{
    assert(i > 0);
    assert(i < N);
    return mVal[i];
}

inline constexpr Quat operator*(Quat lhs, Quat rhs)
{
    // clang-format off
    return {
    lhs.mVal[0] * rhs.mVal[0] - lhs.mVal[1] * rhs.mVal[1] -
    lhs.mVal[2] * rhs.mVal[2] - lhs.mVal[3] * rhs.mVal[3],

    lhs.mVal[0] * rhs.mVal[1] + lhs.mVal[1] * rhs.mVal[0] +
    lhs.mVal[2] * rhs.mVal[3] - lhs.mVal[3] * rhs.mVal[2],

    lhs.mVal[0] * rhs.mVal[2] - lhs.mVal[1] * rhs.mVal[3] +
    lhs.mVal[2] * rhs.mVal[0] + lhs.mVal[3] * rhs.mVal[1],

    lhs.mVal[0] * rhs.mVal[3] + lhs.mVal[1] * rhs.mVal[2] -
    lhs.mVal[2] * rhs.mVal[1] + lhs.mVal[3] * rhs.mVal[0]
    };
    // clang-format on
}

inline constexpr Quat operator*(Quat lhs, Vec3 rhs)
{
    // clang-format off
    return {
    -lhs.mVal[1] * rhs.mVal[0] - lhs.mVal[2] * rhs.mVal[1] - lhs.mVal[3] * rhs.mVal[2],
     lhs.mVal[0] * rhs.mVal[0] + lhs.mVal[2] * rhs.mVal[2] - lhs.mVal[3] * rhs.mVal[1],
     lhs.mVal[0] * rhs.mVal[1] - lhs.mVal[1] * rhs.mVal[2] + lhs.mVal[3] * rhs.mVal[0],
     lhs.mVal[0] * rhs.mVal[2] + lhs.mVal[1] * rhs.mVal[1] - lhs.mVal[2] * rhs.mVal[0]
    };
    // clang-format on
}

inline constexpr Quat operator*(Vec3 lhs, Quat rhs)
{
    // clang-format off
    return {
    -lhs.mVal[0] * rhs.mVal[1] - lhs.mVal[1] * rhs.mVal[2] - lhs.mVal[2] * rhs.mVal[3],
     lhs.mVal[0] * rhs.mVal[0] + lhs.mVal[1] * rhs.mVal[3] - lhs.mVal[2] * rhs.mVal[2],
    -lhs.mVal[0] * rhs.mVal[3] + lhs.mVal[1] * rhs.mVal[0] + lhs.mVal[2] * rhs.mVal[1],
     lhs.mVal[0] * rhs.mVal[2] - lhs.mVal[1] * rhs.mVal[1] + lhs.mVal[2] * rhs.mVal[0]
    };
    // clang-format on
}

[[nodiscard]]
inline constexpr bool operator==(Quat lhs, Quat rhs)
{
    return (lhs.mVal[0] == rhs.mVal[0]) && (lhs.mVal[1] == rhs.mVal[1])
        && (lhs.mVal[2] == rhs.mVal[2]) && (lhs.mVal[3] == rhs.mVal[3]);
}

[[nodiscard]]
inline constexpr bool operator!=(Quat lhs, Quat rhs)
{
    return (lhs.mVal[0] != rhs.mVal[0]) || (lhs.mVal[1] != rhs.mVal[1])
        || (lhs.mVal[2] != rhs.mVal[2]) || (lhs.mVal[3] != lhs.mVal[3]);
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Quat lhs, Quat rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.mVal[0], rhs.mVal[0], tolerance)
        && AlmostEqual(lhs.mVal[1], rhs.mVal[1], tolerance)
        && AlmostEqual(lhs.mVal[2], rhs.mVal[2], tolerance)
        && AlmostEqual(lhs.mVal[3], rhs.mVal[3], tolerance);
}

inline constexpr Quat Normalize(Quat q)
{
    const f32 magSq = q.mVal[0] * q.mVal[0] + q.mVal[1] * q.mVal[1] + q.mVal[2] * q.mVal[2]
        + q.mVal[3] * q.mVal[3];
    assert(magSq != 0.0f);
    const f32 invMag = 1.0f / sqrtf(magSq);
    q.mVal[0] *= invMag;
    q.mVal[1] *= invMag;
    q.mVal[2] *= invMag;
    q.mVal[3] *= invMag;
    return q;
}

inline constexpr Quat Conjugate(Quat q)
{
    return {q.mVal[0], -q.mVal[1], -q.mVal[2], -q.mVal[3]};
}

inline constexpr Vec3 Rotate(Quat lhs, Vec3 rhs)
{
    const Vec3 v = {lhs.mVal[1], lhs.mVal[2], lhs.mVal[3]};
    const Vec3 uv = Cross(v, rhs);
    const Vec3 uuv = Cross(v, uv);

    return rhs + (uuv + uv * lhs.mVal[3]) * 2.0f;
}

inline constexpr Mat3 ToMat3(Quat quat)
{
    const f32 w = quat.mVal[0];
    const f32 x = quat.mVal[1];
    const f32 y = quat.mVal[2];
    const f32 z = quat.mVal[3];
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
    const f32 w = quat.mVal[0];
    const f32 x = quat.mVal[1];
    const f32 y = quat.mVal[2];
    const f32 z = quat.mVal[3];
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
    return {0.0f, v.mVal[0], v.mVal[1], v.mVal[2]};
}

inline constexpr Vec3 ToVec3(Quat q)
{
    return {q.mVal[1], q.mVal[2], q.mVal[3]};
}

inline constexpr void Clear(Quat& q)
{
    q.mVal[0] = 0.0f;
    q.mVal[1] = 0.0f;
    q.mVal[2] = 0.0f;
    q.mVal[3] = 0.0f;
}
