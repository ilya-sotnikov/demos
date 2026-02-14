#pragma once

#include "MathCommon.hpp"
#include "Vec3.hpp"
#include "Vec4.hpp"
#include "Quat.hpp"

inline constexpr Mat4 Mat4::Identity()
{
    Mat4 m{};
    m.col[0].val[0] = 1.0f;
    m.col[1].val[1] = 1.0f;
    m.col[2].val[2] = 1.0f;
    m.col[3].val[3] = 1.0f;
    return m;
}

inline constexpr Mat4 Mat4::Zero()
{
    return {};
}

inline constexpr f32 Mat4::operator()(int row, int column) const
{
    DEBUG_ASSERT(row >= 0);
    DEBUG_ASSERT(column >= 0);
    DEBUG_ASSERT(row < N);
    DEBUG_ASSERT(column < N);
    return col[column].val[row];
}

inline constexpr f32& Mat4::operator()(int row, int column)
{
    DEBUG_ASSERT(row >= 0);
    DEBUG_ASSERT(column >= 0);
    DEBUG_ASSERT(row < N);
    DEBUG_ASSERT(column < N);
    return col[column].val[row];
}

inline constexpr Mat4 operator-(const Mat4& v)
{
    return {-v.col[0], -v.col[1], -v.col[2], -v.col[3]};
}

inline constexpr Mat4& operator+=(Mat4& lhs, const Mat4& rhs)
{
    lhs.col[0] += rhs.col[0];
    lhs.col[1] += rhs.col[1];
    lhs.col[2] += rhs.col[2];
    lhs.col[3] += rhs.col[3];
    return lhs;
}

inline constexpr Mat4& operator-=(Mat4& lhs, const Mat4& rhs)
{
    lhs.col[0] -= rhs.col[0];
    lhs.col[1] -= rhs.col[1];
    lhs.col[2] -= rhs.col[2];
    lhs.col[3] -= rhs.col[3];
    return lhs;
}

inline constexpr Mat4& operator*=(Mat4& lhs, f32 rhs)
{
    lhs.col[0] *= rhs;
    lhs.col[1] *= rhs;
    lhs.col[2] *= rhs;
    lhs.col[3] *= rhs;
    return lhs;
}

inline constexpr Mat4& operator/=(Mat4& lhs, f32 rhs)
{
    lhs.col[0] /= rhs;
    lhs.col[1] /= rhs;
    lhs.col[2] /= rhs;
    lhs.col[3] /= rhs;
    return lhs;
}

inline constexpr Mat4 operator+(const Mat4& lhs, const Mat4& rhs)
{
    return {
        lhs.col[0] + rhs.col[0],
        lhs.col[1] + rhs.col[1],
        lhs.col[2] + rhs.col[2],
        lhs.col[3] + rhs.col[3]
    };
}

inline constexpr Mat4 operator-(const Mat4& lhs, const Mat4& rhs)
{
    return {
        lhs.col[0] - rhs.col[0],
        lhs.col[1] - rhs.col[1],
        lhs.col[2] - rhs.col[2],
        lhs.col[3] - rhs.col[3]
    };
}

inline constexpr Mat4 operator*(const Mat4& lhs, f32 rhs)
{
    return {lhs.col[0] * rhs, lhs.col[1] * rhs, lhs.col[2] * rhs, lhs.col[3] * rhs};
}

inline constexpr Mat4 operator/(const Mat4& lhs, f32 rhs)
{
    return {lhs.col[0] / rhs, lhs.col[1] / rhs, lhs.col[2] / rhs, lhs.col[3] / rhs};
}

inline constexpr Mat4 operator*(f32 lhs, const Mat4& rhs)
{
    return {lhs * rhs.col[0], lhs * rhs.col[1], lhs * rhs.col[2], lhs * rhs.col[3]};
}

inline constexpr Vec4 operator*(const Mat4& lhs, Vec4 rhs)
{
    // clang-format off
    return {
        lhs.col[0].val[0] * rhs.val[0] +
        lhs.col[1].val[0] * rhs.val[1] +
        lhs.col[2].val[0] * rhs.val[2] +
        lhs.col[3].val[0] * rhs.val[3],

        lhs.col[0].val[1] * rhs.val[0] +
        lhs.col[1].val[1] * rhs.val[1] +
        lhs.col[2].val[1] * rhs.val[2] +
        lhs.col[3].val[1] * rhs.val[3],

        lhs.col[0].val[2] * rhs.val[0] +
        lhs.col[1].val[2] * rhs.val[1] +
        lhs.col[2].val[2] * rhs.val[2] +
        lhs.col[3].val[2] * rhs.val[3],

        lhs.col[0].val[3] * rhs.val[0] +
        lhs.col[1].val[3] * rhs.val[1] +
        lhs.col[2].val[3] * rhs.val[2] +
        lhs.col[3].val[3] * rhs.val[3]
    };
    // clang-format on
}

inline constexpr Mat4 operator*(const Mat4& lhs, const Mat4& rhs)
{
    return {lhs * rhs.col[0], lhs * rhs.col[1], lhs * rhs.col[2], lhs * rhs.col[3]};
}

inline constexpr Vec4 TMul(const Mat4& lhsT, Vec4 rhs)
{
    // clang-format off
    return {
        lhsT.col[0].val[0] * rhs.val[0] +
        lhsT.col[0].val[1] * rhs.val[1] +
        lhsT.col[0].val[2] * rhs.val[2] +
        lhsT.col[0].val[3] * rhs.val[3],

        lhsT.col[1].val[0] * rhs.val[0] +
        lhsT.col[1].val[1] * rhs.val[1] +
        lhsT.col[1].val[2] * rhs.val[2] +
        lhsT.col[1].val[3] * rhs.val[3],

        lhsT.col[2].val[0] * rhs.val[0] +
        lhsT.col[2].val[1] * rhs.val[1] +
        lhsT.col[2].val[2] * rhs.val[2] +
        lhsT.col[2].val[3] * rhs.val[3],

        lhsT.col[3].val[0] * rhs.val[0] +
        lhsT.col[3].val[1] * rhs.val[1] +
        lhsT.col[3].val[2] * rhs.val[2] +
        lhsT.col[3].val[3] * rhs.val[3]
    };
    // clang-format on
}

inline Mat4 TMul(const Mat4& lhs, const Mat4& rhs)
{
    Mat4 res;
    for (int i = 0; i < Mat4::N; ++i)
    {
        for (int j = 0; j < Mat4::N; ++j)
        {
            res.col[i].val[j] = Dot(lhs.col[j], rhs.col[i]);
        }
    }
    return res;
}

[[nodiscard]]
inline constexpr bool operator==(const Mat4& lhs, const Mat4& rhs)
{
    // clang-format off
    return
        (lhs.col[0] == rhs.col[0]) &&
        (lhs.col[1] == rhs.col[1]) &&
        (lhs.col[2] == rhs.col[2]) &&
        (lhs.col[3] == rhs.col[3]);
    // clang-format on
}

[[nodiscard]]
inline constexpr bool operator!=(const Mat4& lhs, const Mat4& rhs)
{
    // clang-format off
    return
        (lhs.col[0] != rhs.col[0]) ||
        (lhs.col[1] != rhs.col[1]) ||
        (lhs.col[2] != rhs.col[2]) ||
        (lhs.col[3] != rhs.col[3]);
    // clang-format on
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Mat4 lhs, Mat4 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.col[0], rhs.col[0], tolerance)
        && AlmostEqual(lhs.col[1], rhs.col[1], tolerance)
        && AlmostEqual(lhs.col[2], rhs.col[2], tolerance)
        && AlmostEqual(lhs.col[3], rhs.col[3], tolerance);
}

inline constexpr Mat4 Transpose(const Mat4& m)
{
    // clang-format off
    return {
        m.col[0].val[0], m.col[1].val[0], m.col[2].val[0], m.col[3].val[0],
        m.col[0].val[1], m.col[1].val[1], m.col[2].val[1], m.col[3].val[1],
        m.col[0].val[2], m.col[1].val[2], m.col[2].val[2], m.col[3].val[2],
        m.col[0].val[3], m.col[1].val[3], m.col[2].val[3], m.col[3].val[3]
    };
    // clang-format on
}

inline constexpr Mat4 Translate(const Mat4& m, Vec3 v)
{
    Mat4 res{m};

    const f32 m00 = m.col[0].val[0];
    const f32 m01 = m.col[0].val[1];
    const f32 m02 = m.col[0].val[2];
    const f32 m03 = m.col[0].val[3];

    const f32 m10 = m.col[1].val[0];
    const f32 m11 = m.col[1].val[1];
    const f32 m12 = m.col[1].val[2];
    const f32 m13 = m.col[1].val[3];

    const f32 m20 = m.col[2].val[0];
    const f32 m21 = m.col[2].val[1];
    const f32 m22 = m.col[2].val[2];
    const f32 m23 = m.col[2].val[3];

    const f32 m30 = m.col[3].val[0];
    const f32 m31 = m.col[3].val[1];
    const f32 m32 = m.col[3].val[2];
    const f32 m33 = m.col[3].val[3];

    // result of matrix multiplication m * t,
    res.col[3].val[0] = m00 * v.val[0] + m10 * v.val[1] + m20 * v.val[2] + m30;
    res.col[3].val[1] = m01 * v.val[0] + m11 * v.val[1] + m21 * v.val[2] + m31;
    res.col[3].val[2] = m02 * v.val[0] + m12 * v.val[1] + m22 * v.val[2] + m32;
    res.col[3].val[3] = m03 * v.val[0] + m13 * v.val[1] + m23 * v.val[2] + m33;

    return res;
}

inline constexpr Mat4 Scale(const Mat4& m, Vec3 scale)
{
    Mat4 res{m};

    // Result of matrix multiplication m * s.
    res.col[0].val[0] *= scale.val[0];
    res.col[0].val[1] *= scale.val[0];
    res.col[0].val[2] *= scale.val[0];
    res.col[0].val[3] *= scale.val[0];
    res.col[1].val[0] *= scale.val[1];
    res.col[1].val[1] *= scale.val[1];
    res.col[1].val[2] *= scale.val[1];
    res.col[1].val[3] *= scale.val[1];
    res.col[2].val[0] *= scale.val[2];
    res.col[2].val[1] *= scale.val[2];
    res.col[2].val[2] *= scale.val[2];
    res.col[2].val[3] *= scale.val[2];

    return res;
}

inline constexpr Mat4 Scale(const Mat4& m, f32 scale)
{
    Mat4 res{m};

    // Result of matrix multiplication m * s.
    res.col[0].val[0] *= scale;
    res.col[0].val[1] *= scale;
    res.col[0].val[2] *= scale;
    res.col[0].val[3] *= scale;
    res.col[1].val[0] *= scale;
    res.col[1].val[1] *= scale;
    res.col[1].val[2] *= scale;
    res.col[1].val[3] *= scale;
    res.col[2].val[0] *= scale;
    res.col[2].val[1] *= scale;
    res.col[2].val[2] *= scale;
    res.col[2].val[3] *= scale;

    return res;
}

inline Mat4 LookAt(Vec3 position, Vec3 target, Vec3 worldUp)
{
    const Vec3 axisZ = Normalize(target - position);
    const Vec3 axisX = Normalize(Cross(axisZ, worldUp));
    const Vec3 axisY = Cross(axisX, axisZ);

    Mat4 result;

    // Rotation component.
    result.col[0].val[0] = axisX.val[0];
    result.col[0].val[1] = axisY.val[0];
    result.col[0].val[2] = -axisZ.val[0];
    result.col[0].val[3] = 0.0f;
    result.col[1].val[0] = axisX.val[1];
    result.col[1].val[1] = axisY.val[1];
    result.col[1].val[2] = -axisZ.val[1];
    result.col[1].val[3] = 0.0f;
    result.col[2].val[0] = axisX.val[2];
    result.col[2].val[1] = axisY.val[2];
    result.col[2].val[2] = -axisZ.val[2];
    result.col[2].val[3] = 0.0f;

    // Translation component (with rotation applied, therefore
    // it's not just -position.X, ..., but a dot product).
    result.col[3].val[0] = -Dot(position, axisX);
    result.col[3].val[1] = -Dot(position, axisY);
    result.col[3].val[2] = Dot(position, axisZ);
    result.col[3].val[3] = 1.0f;

    return result;
}

// Infinite perspective projection with reversed Z.
// https://nlguillemot.wordpress.com/2016/12/07/reversed-z-in-opengl/
inline constexpr Mat4 Perspective(f32 fovYRad, f32 aspect, f32 zNear)
{
    DEBUG_ASSERT(fovYRad > 0.0f);
    DEBUG_ASSERT(aspect > 0.0f);
    DEBUG_ASSERT(zNear > 0.0f);

    Mat4 res{};

    const f32 invTanHalfFovY = 1.0f / tanf(fovYRad / 2.0f);

    res.col[0].val[0] = invTanHalfFovY / aspect;
    res.col[1].val[1] = invTanHalfFovY;
    res.col[2].val[2] = 0.0f;
    res.col[2].val[3] = -1.0f;
    res.col[3].val[2] = zNear;

    return res;
}

inline constexpr Mat4 Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far)
{
    DEBUG_ASSERT(right - left != 0.0f);
    DEBUG_ASSERT(near - far != 0.0f);
    DEBUG_ASSERT(top - bottom != 0.0f);

    Mat4 res{};

    res.col[0].val[0] = 2.0f / (right - left);
    res.col[1].val[1] = 2.0f / (top - bottom);
    res.col[2].val[2] = -1.0f / (far - near);
    res.col[3].val[0] = -(right + left) / (right - left);
    res.col[3].val[1] = -(top + bottom) / (top - bottom);
    res.col[3].val[2] = -near / (far - near);
    res.col[3].val[3] = 1.0f;

    return res;
}

inline constexpr Mat4 Viewport(u32 x, u32 y, u32 w, u32 h)
{
    DEBUG_ASSERT(w > 0);
    DEBUG_ASSERT(h > 0);

    // clang-format off
    return {
        f32(w) / 2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, f32(h) / 2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        f32(x) + f32(w) / 2.0f, f32(y) + f32(h) / 2.0f, 0.0f, 1.0f
    };
    // clang-format on
}

inline constexpr Mat4 Model(Vec3 position, Quat orientation, Vec3 scale)
{
    Mat4 res = {};

    res.col[0].val[0] = scale.val[0];
    res.col[0].val[1] = scale.val[0];
    res.col[0].val[2] = scale.val[0];
    res.col[1].val[0] = scale.val[1];
    res.col[1].val[1] = scale.val[1];
    res.col[1].val[2] = scale.val[1];
    res.col[2].val[0] = scale.val[2];
    res.col[2].val[1] = scale.val[2];
    res.col[2].val[2] = scale.val[2];
    res.col[3].val[3] = 1.0f;

    const Mat4 rot = ToMat4(orientation);

    res.col[0].val[0] *= rot.col[0].val[0];
    res.col[0].val[1] *= rot.col[0].val[1];
    res.col[0].val[2] *= rot.col[0].val[2];
    res.col[1].val[0] *= rot.col[1].val[0];
    res.col[1].val[1] *= rot.col[1].val[1];
    res.col[1].val[2] *= rot.col[1].val[2];
    res.col[2].val[0] *= rot.col[2].val[0];
    res.col[2].val[1] *= rot.col[2].val[1];
    res.col[2].val[2] *= rot.col[2].val[2];

    res.col[3].val[0] = position.val[0];
    res.col[3].val[1] = position.val[1];
    res.col[3].val[2] = position.val[2];

    return res;
}

inline constexpr Mat4 Model(Vec3 position, Quat orientation, f32 scale)
{
    Mat4 res = {};

    res.col[0].val[0] = scale;
    res.col[0].val[1] = scale;
    res.col[0].val[2] = scale;
    res.col[1].val[0] = scale;
    res.col[1].val[1] = scale;
    res.col[1].val[2] = scale;
    res.col[2].val[0] = scale;
    res.col[2].val[1] = scale;
    res.col[2].val[2] = scale;
    res.col[3].val[3] = 1.0f;

    const Mat4 rot = ToMat4(orientation);

    res.col[0].val[0] *= rot.col[0].val[0];
    res.col[0].val[1] *= rot.col[0].val[1];
    res.col[0].val[2] *= rot.col[0].val[2];
    res.col[1].val[0] *= rot.col[1].val[0];
    res.col[1].val[1] *= rot.col[1].val[1];
    res.col[1].val[2] *= rot.col[1].val[2];
    res.col[2].val[0] *= rot.col[2].val[0];
    res.col[2].val[1] *= rot.col[2].val[1];
    res.col[2].val[2] *= rot.col[2].val[2];

    res.col[3].val[0] = position.val[0];
    res.col[3].val[1] = position.val[1];
    res.col[3].val[2] = position.val[2];

    return res;
}

inline constexpr Mat3 ToMat3(const Mat4& m)
{
    // clang-format off
    return {
        m.col[0].val[0], m.col[0].val[1], m.col[0].val[2],
        m.col[1].val[0], m.col[1].val[1], m.col[1].val[2],
        m.col[2].val[0], m.col[2].val[1], m.col[2].val[2],
    };
    // clang-format on
}

// The Laplace Expansion Theorem: Computing the Determinants and Inverses of Matrices
// by David Eberly:
// https://www.geometrictools.com/Documentation/LaplaceExpansionTheorem.pdf
inline Mat4 Inverse(const Mat4& m)
{
    const f32 s0 = m.col[0].val[0] * m.col[1].val[1] - m.col[0].val[1] * m.col[1].val[0];
    const f32 s1 = m.col[0].val[0] * m.col[2].val[1] - m.col[0].val[1] * m.col[2].val[0];
    const f32 s2 = m.col[0].val[0] * m.col[3].val[1] - m.col[0].val[1] * m.col[3].val[0];
    const f32 s3 = m.col[1].val[0] * m.col[2].val[1] - m.col[1].val[1] * m.col[2].val[0];
    const f32 s4 = m.col[1].val[0] * m.col[3].val[1] - m.col[1].val[1] * m.col[3].val[0];
    const f32 s5 = m.col[2].val[0] * m.col[3].val[1] - m.col[2].val[1] * m.col[3].val[0];

    const f32 c5 = m.col[2].val[2] * m.col[3].val[3] - m.col[2].val[3] * m.col[3].val[2];
    const f32 c4 = m.col[1].val[2] * m.col[3].val[3] - m.col[1].val[3] * m.col[3].val[2];
    const f32 c3 = m.col[1].val[2] * m.col[2].val[3] - m.col[1].val[3] * m.col[2].val[2];
    const f32 c2 = m.col[0].val[2] * m.col[3].val[3] - m.col[0].val[3] * m.col[3].val[2];
    const f32 c1 = m.col[0].val[2] * m.col[2].val[3] - m.col[0].val[3] * m.col[2].val[2];
    const f32 c0 = m.col[0].val[2] * m.col[1].val[3] - m.col[0].val[3] * m.col[1].val[2];

    const f32 invDet = 1.0f / (s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0);
    DEBUG_ASSERT(invDet != 0.0f);

    Mat4 result;

    result.col[0].val[0]
        = (m.col[1].val[1] * c5 - m.col[2].val[1] * c4 + m.col[3].val[1] * c3) * invDet;
    result.col[1].val[0]
        = (-m.col[1].val[0] * c5 + m.col[2].val[0] * c4 - m.col[3].val[0] * c3) * invDet;
    result.col[2].val[0]
        = (m.col[1].val[3] * s5 - m.col[2].val[3] * s4 + m.col[3].val[3] * s3) * invDet;
    result.col[3].val[0]
        = (-m.col[1].val[2] * s5 + m.col[2].val[2] * s4 - m.col[3].val[2] * s3) * invDet;

    result.col[0].val[1]
        = (-m.col[0].val[1] * c5 + m.col[2].val[1] * c2 - m.col[3].val[1] * c1) * invDet;
    result.col[1].val[1]
        = (m.col[0].val[0] * c5 - m.col[2].val[0] * c2 + m.col[3].val[0] * c1) * invDet;
    result.col[2].val[1]
        = (-m.col[0].val[3] * s5 + m.col[2].val[3] * s2 - m.col[3].val[3] * s1) * invDet;
    result.col[3].val[1]
        = (m.col[0].val[2] * s5 - m.col[2].val[2] * s2 + m.col[3].val[2] * s1) * invDet;

    result.col[0].val[2]
        = (m.col[0].val[1] * c4 - m.col[1].val[1] * c2 + m.col[3].val[1] * c0) * invDet;
    result.col[1].val[2]
        = (-m.col[0].val[0] * c4 + m.col[1].val[0] * c2 - m.col[3].val[0] * c0) * invDet;
    result.col[2].val[2]
        = (m.col[0].val[3] * s4 - m.col[1].val[3] * s2 + m.col[3].val[3] * s0) * invDet;
    result.col[3].val[2]
        = (-m.col[0].val[2] * s4 + m.col[1].val[2] * s2 - m.col[3].val[2] * s0) * invDet;

    result.col[0].val[3]
        = (-m.col[0].val[1] * c3 + m.col[1].val[1] * c1 - m.col[2].val[1] * c0) * invDet;
    result.col[1].val[3]
        = (m.col[0].val[0] * c3 - m.col[1].val[0] * c1 + m.col[2].val[0] * c0) * invDet;
    result.col[2].val[3]
        = (-m.col[0].val[3] * s3 + m.col[1].val[3] * s1 - m.col[2].val[3] * s0) * invDet;
    result.col[3].val[3]
        = (m.col[0].val[2] * s3 - m.col[1].val[2] * s1 + m.col[2].val[2] * s0) * invDet;

    return result;
}
