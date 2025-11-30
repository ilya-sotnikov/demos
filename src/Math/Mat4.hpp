#pragma once

#include "MathCommon.hpp"
#include "Vec3.hpp"
#include "Vec4.hpp"
#include "Quat.hpp"

inline constexpr Mat4 Mat4::Identity()
{
    Mat4 m{};
    m.mCol[0].mVal[0] = 1.0f;
    m.mCol[1].mVal[1] = 1.0f;
    m.mCol[2].mVal[2] = 1.0f;
    m.mCol[3].mVal[3] = 1.0f;
    return m;
}

inline constexpr Mat4 Mat4::Zero()
{
    return {};
}

inline constexpr f32 Mat4::operator()(int row, int col) const
{
    assert(row >= 0);
    assert(col >= 0);
    assert(row < N);
    assert(col < N);
    return mCol[col].mVal[row];
}

inline constexpr f32& Mat4::operator()(int row, int col)
{
    assert(row >= 0);
    assert(col >= 0);
    assert(row < N);
    assert(col < N);
    return mCol[col].mVal[row];
}

inline constexpr Mat4 operator-(const Mat4& v)
{
    return {-v.mCol[0], -v.mCol[1], -v.mCol[2], -v.mCol[3]};
}

inline constexpr Mat4& operator+=(Mat4& lhs, const Mat4& rhs)
{
    lhs.mCol[0] += rhs.mCol[0];
    lhs.mCol[1] += rhs.mCol[1];
    lhs.mCol[2] += rhs.mCol[2];
    lhs.mCol[3] += rhs.mCol[3];
    return lhs;
}

inline constexpr Mat4& operator-=(Mat4& lhs, const Mat4& rhs)
{
    lhs.mCol[0] -= rhs.mCol[0];
    lhs.mCol[1] -= rhs.mCol[1];
    lhs.mCol[2] -= rhs.mCol[2];
    lhs.mCol[3] -= rhs.mCol[3];
    return lhs;
}

inline constexpr Mat4& operator*=(Mat4& lhs, f32 rhs)
{
    lhs.mCol[0] *= rhs;
    lhs.mCol[1] *= rhs;
    lhs.mCol[2] *= rhs;
    lhs.mCol[3] *= rhs;
    return lhs;
}

inline constexpr Mat4& operator/=(Mat4& lhs, f32 rhs)
{
    lhs.mCol[0] /= rhs;
    lhs.mCol[1] /= rhs;
    lhs.mCol[2] /= rhs;
    lhs.mCol[3] /= rhs;
    return lhs;
}

inline constexpr Mat4 operator+(const Mat4& lhs, const Mat4& rhs)
{
    return {
        lhs.mCol[0] + rhs.mCol[0],
        lhs.mCol[1] + rhs.mCol[1],
        lhs.mCol[2] + rhs.mCol[2],
        lhs.mCol[3] + rhs.mCol[3]
    };
}

inline constexpr Mat4 operator-(const Mat4& lhs, const Mat4& rhs)
{
    return {
        lhs.mCol[0] - rhs.mCol[0],
        lhs.mCol[1] - rhs.mCol[1],
        lhs.mCol[2] - rhs.mCol[2],
        lhs.mCol[3] - rhs.mCol[3]
    };
}

inline constexpr Mat4 operator*(const Mat4& lhs, f32 rhs)
{
    return {lhs.mCol[0] * rhs, lhs.mCol[1] * rhs, lhs.mCol[2] * rhs, lhs.mCol[3] * rhs};
}

inline constexpr Mat4 operator/(const Mat4& lhs, f32 rhs)
{
    return {lhs.mCol[0] / rhs, lhs.mCol[1] / rhs, lhs.mCol[2] / rhs, lhs.mCol[3] / rhs};
}

inline constexpr Mat4 operator*(f32 lhs, const Mat4& rhs)
{
    return {lhs * rhs.mCol[0], lhs * rhs.mCol[1], lhs * rhs.mCol[2], lhs * rhs.mCol[3]};
}

inline constexpr Vec4 operator*(const Mat4& lhs, Vec4 rhs)
{
    // clang-format off
    return {
        lhs.mCol[0].mVal[0] * rhs.mVal[0] +
        lhs.mCol[1].mVal[0] * rhs.mVal[1] +
        lhs.mCol[2].mVal[0] * rhs.mVal[2] +
        lhs.mCol[3].mVal[0] * rhs.mVal[3],

        lhs.mCol[0].mVal[1] * rhs.mVal[0] +
        lhs.mCol[1].mVal[1] * rhs.mVal[1] +
        lhs.mCol[2].mVal[1] * rhs.mVal[2] +
        lhs.mCol[3].mVal[1] * rhs.mVal[3],

        lhs.mCol[0].mVal[2] * rhs.mVal[0] +
        lhs.mCol[1].mVal[2] * rhs.mVal[1] +
        lhs.mCol[2].mVal[2] * rhs.mVal[2] +
        lhs.mCol[3].mVal[2] * rhs.mVal[3],

        lhs.mCol[0].mVal[3] * rhs.mVal[0] +
        lhs.mCol[1].mVal[3] * rhs.mVal[1] +
        lhs.mCol[2].mVal[3] * rhs.mVal[2] +
        lhs.mCol[3].mVal[3] * rhs.mVal[3]
    };
    // clang-format on
}

inline constexpr Mat4 operator*(const Mat4& lhs, const Mat4& rhs)
{
    return {lhs * rhs.mCol[0], lhs * rhs.mCol[1], lhs * rhs.mCol[2], lhs * rhs.mCol[3]};
}

inline constexpr Vec4 TMul(const Mat4& lhsT, Vec4 rhs)
{
    // clang-format off
    return {
        lhsT.mCol[0].mVal[0] * rhs.mVal[0] +
        lhsT.mCol[0].mVal[1] * rhs.mVal[1] +
        lhsT.mCol[0].mVal[2] * rhs.mVal[2] +
        lhsT.mCol[0].mVal[3] * rhs.mVal[3],

        lhsT.mCol[1].mVal[0] * rhs.mVal[0] +
        lhsT.mCol[1].mVal[1] * rhs.mVal[1] +
        lhsT.mCol[1].mVal[2] * rhs.mVal[2] +
        lhsT.mCol[1].mVal[3] * rhs.mVal[3],

        lhsT.mCol[2].mVal[0] * rhs.mVal[0] +
        lhsT.mCol[2].mVal[1] * rhs.mVal[1] +
        lhsT.mCol[2].mVal[2] * rhs.mVal[2] +
        lhsT.mCol[2].mVal[3] * rhs.mVal[3],

        lhsT.mCol[3].mVal[0] * rhs.mVal[0] +
        lhsT.mCol[3].mVal[1] * rhs.mVal[1] +
        lhsT.mCol[3].mVal[2] * rhs.mVal[2] +
        lhsT.mCol[3].mVal[3] * rhs.mVal[3]
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
            res.mCol[i].mVal[j] = Dot(lhs.mCol[j], rhs.mCol[i]);
        }
    }
    return res;
}

[[nodiscard]]
inline constexpr bool operator==(const Mat4& lhs, const Mat4& rhs)
{
    // clang-format off
    return
        (lhs.mCol[0] == rhs.mCol[0]) &&
        (lhs.mCol[1] == rhs.mCol[1]) &&
        (lhs.mCol[2] == rhs.mCol[2]) &&
        (lhs.mCol[3] == rhs.mCol[3]);
    // clang-format on
}

[[nodiscard]]
inline constexpr bool operator!=(const Mat4& lhs, const Mat4& rhs)
{
    // clang-format off
    return
        (lhs.mCol[0] != rhs.mCol[0]) ||
        (lhs.mCol[1] != rhs.mCol[1]) ||
        (lhs.mCol[2] != rhs.mCol[2]) ||
        (lhs.mCol[3] != rhs.mCol[3]);
    // clang-format on
}

[[nodiscard]]
inline constexpr bool AlmostEqual(Mat4 lhs, Mat4 rhs, f32 tolerance = FLT_EPSILON)
{
    return AlmostEqual(lhs.mCol[0], rhs.mCol[0], tolerance)
        && AlmostEqual(lhs.mCol[1], rhs.mCol[1], tolerance)
        && AlmostEqual(lhs.mCol[2], rhs.mCol[2], tolerance)
        && AlmostEqual(lhs.mCol[3], rhs.mCol[3], tolerance);
}

inline constexpr Mat4 Transpose(const Mat4& m)
{
    // clang-format off
    return {
        m.mCol[0].mVal[0], m.mCol[1].mVal[0], m.mCol[2].mVal[0], m.mCol[3].mVal[0],
        m.mCol[0].mVal[1], m.mCol[1].mVal[1], m.mCol[2].mVal[1], m.mCol[3].mVal[1],
        m.mCol[0].mVal[2], m.mCol[1].mVal[2], m.mCol[2].mVal[2], m.mCol[3].mVal[2],
        m.mCol[0].mVal[3], m.mCol[1].mVal[3], m.mCol[2].mVal[3], m.mCol[3].mVal[3]
    };
    // clang-format on
}

inline constexpr Mat4 Translate(const Mat4& m, Vec3 v)
{
    Mat4 res{m};

    const f32 m00 = m.mCol[0].mVal[0];
    const f32 m01 = m.mCol[0].mVal[1];
    const f32 m02 = m.mCol[0].mVal[2];
    const f32 m03 = m.mCol[0].mVal[3];

    const f32 m10 = m.mCol[1].mVal[0];
    const f32 m11 = m.mCol[1].mVal[1];
    const f32 m12 = m.mCol[1].mVal[2];
    const f32 m13 = m.mCol[1].mVal[3];

    const f32 m20 = m.mCol[2].mVal[0];
    const f32 m21 = m.mCol[2].mVal[1];
    const f32 m22 = m.mCol[2].mVal[2];
    const f32 m23 = m.mCol[2].mVal[3];

    const f32 m30 = m.mCol[3].mVal[0];
    const f32 m31 = m.mCol[3].mVal[1];
    const f32 m32 = m.mCol[3].mVal[2];
    const f32 m33 = m.mCol[3].mVal[3];

    // result of matrix multiplication m * t,
    res.mCol[3].mVal[0] = m00 * v.mVal[0] + m10 * v.mVal[1] + m20 * v.mVal[2] + m30;
    res.mCol[3].mVal[1] = m01 * v.mVal[0] + m11 * v.mVal[1] + m21 * v.mVal[2] + m31;
    res.mCol[3].mVal[2] = m02 * v.mVal[0] + m12 * v.mVal[1] + m22 * v.mVal[2] + m32;
    res.mCol[3].mVal[3] = m03 * v.mVal[0] + m13 * v.mVal[1] + m23 * v.mVal[2] + m33;

    return res;
}

inline constexpr Mat4 Scale(const Mat4& m, Vec3 scale)
{
    Mat4 res{m};

    // Result of matrix multiplication m * s.
    res.mCol[0].mVal[0] *= scale.mVal[0];
    res.mCol[0].mVal[1] *= scale.mVal[0];
    res.mCol[0].mVal[2] *= scale.mVal[0];
    res.mCol[0].mVal[3] *= scale.mVal[0];
    res.mCol[1].mVal[0] *= scale.mVal[1];
    res.mCol[1].mVal[1] *= scale.mVal[1];
    res.mCol[1].mVal[2] *= scale.mVal[1];
    res.mCol[1].mVal[3] *= scale.mVal[1];
    res.mCol[2].mVal[0] *= scale.mVal[2];
    res.mCol[2].mVal[1] *= scale.mVal[2];
    res.mCol[2].mVal[2] *= scale.mVal[2];
    res.mCol[2].mVal[3] *= scale.mVal[2];

    return res;
}

inline constexpr Mat4 Scale(const Mat4& m, f32 scale)
{
    Mat4 res{m};

    // Result of matrix multiplication m * s.
    res.mCol[0].mVal[0] *= scale;
    res.mCol[0].mVal[1] *= scale;
    res.mCol[0].mVal[2] *= scale;
    res.mCol[0].mVal[3] *= scale;
    res.mCol[1].mVal[0] *= scale;
    res.mCol[1].mVal[1] *= scale;
    res.mCol[1].mVal[2] *= scale;
    res.mCol[1].mVal[3] *= scale;
    res.mCol[2].mVal[0] *= scale;
    res.mCol[2].mVal[1] *= scale;
    res.mCol[2].mVal[2] *= scale;
    res.mCol[2].mVal[3] *= scale;

    return res;
}

inline Mat4 LookAt(Vec3 position, Vec3 target, Vec3 worldUp)
{
    const Vec3 axisZ = Normalize(target - position);
    const Vec3 axisX = Normalize(Cross(axisZ, worldUp));
    const Vec3 axisY = Cross(axisX, axisZ);

    Mat4 result;

    // Rotation component.
    result.mCol[0].mVal[0] = axisX.mVal[0];
    result.mCol[0].mVal[1] = axisY.mVal[0];
    result.mCol[0].mVal[2] = -axisZ.mVal[0];
    result.mCol[0].mVal[3] = 0.0f;
    result.mCol[1].mVal[0] = axisX.mVal[1];
    result.mCol[1].mVal[1] = axisY.mVal[1];
    result.mCol[1].mVal[2] = -axisZ.mVal[1];
    result.mCol[1].mVal[3] = 0.0f;
    result.mCol[2].mVal[0] = axisX.mVal[2];
    result.mCol[2].mVal[1] = axisY.mVal[2];
    result.mCol[2].mVal[2] = -axisZ.mVal[2];
    result.mCol[2].mVal[3] = 0.0f;

    // Translation component (with rotation applied, therefore
    // it's not just -position.X, ..., but a dot product).
    result.mCol[3].mVal[0] = -Dot(position, axisX);
    result.mCol[3].mVal[1] = -Dot(position, axisY);
    result.mCol[3].mVal[2] = Dot(position, axisZ);
    result.mCol[3].mVal[3] = 1.0f;

    return result;
}

// Infinite perspective projection with reversed Z.
// https://nlguillemot.wordpress.com/2016/12/07/reversed-z-in-opengl/
inline constexpr Mat4 Perspective(f32 fovYRad, f32 aspect, f32 zNear)
{
    assert(fovYRad > 0.0f);
    assert(fovYRad <= 120.0f);
    assert(aspect > 0.0f);
    assert(zNear > 0.0f);

    Mat4 res{};

    const f32 invTanHalfFovY = 1.0f / tanf(fovYRad / 2.0f);

    res.mCol[0].mVal[0] = invTanHalfFovY / aspect;
    res.mCol[1].mVal[1] = invTanHalfFovY;
    res.mCol[2].mVal[2] = 0.0f;
    res.mCol[2].mVal[3] = -1.0f;
    res.mCol[3].mVal[2] = zNear;

    return res;
}

inline constexpr Mat4 Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far)
{
    assert(right - left != 0.0f);
    assert(near - far != 0.0f);
    assert(top - bottom != 0.0f);

    Mat4 res{};

    res.mCol[0].mVal[0] = 2.0f / (right - left);
    res.mCol[1].mVal[1] = 2.0f / (top - bottom);
    res.mCol[2].mVal[2] = -1.0f / (far - near);
    res.mCol[3].mVal[0] = -(right + left) / (right - left);
    res.mCol[3].mVal[1] = -(top + bottom) / (top - bottom);
    res.mCol[3].mVal[2] = -near / (far - near);
    res.mCol[3].mVal[3] = 1.0f;

    return res;
}

inline constexpr Mat4 Model(Vec3 position, Quat orientation, Vec3 scale)
{
    Mat4 res = {};

    res.mCol[0].mVal[0] = scale.mVal[0];
    res.mCol[0].mVal[1] = scale.mVal[0];
    res.mCol[0].mVal[2] = scale.mVal[0];
    res.mCol[1].mVal[0] = scale.mVal[1];
    res.mCol[1].mVal[1] = scale.mVal[1];
    res.mCol[1].mVal[2] = scale.mVal[1];
    res.mCol[2].mVal[0] = scale.mVal[2];
    res.mCol[2].mVal[1] = scale.mVal[2];
    res.mCol[2].mVal[2] = scale.mVal[2];
    res.mCol[3].mVal[3] = 1.0f;

    const Mat4 rot = ToMat4(orientation);

    res.mCol[0].mVal[0] *= rot.mCol[0].mVal[0];
    res.mCol[0].mVal[1] *= rot.mCol[0].mVal[1];
    res.mCol[0].mVal[2] *= rot.mCol[0].mVal[2];
    res.mCol[1].mVal[0] *= rot.mCol[1].mVal[0];
    res.mCol[1].mVal[1] *= rot.mCol[1].mVal[1];
    res.mCol[1].mVal[2] *= rot.mCol[1].mVal[2];
    res.mCol[2].mVal[0] *= rot.mCol[2].mVal[0];
    res.mCol[2].mVal[1] *= rot.mCol[2].mVal[1];
    res.mCol[2].mVal[2] *= rot.mCol[2].mVal[2];

    res.mCol[3].mVal[0] = position.mVal[0];
    res.mCol[3].mVal[1] = position.mVal[1];
    res.mCol[3].mVal[2] = position.mVal[2];

    return res;
}

inline constexpr Mat4 Model(Vec3 position, Quat orientation, f32 scale)
{
    Mat4 res = {};

    res.mCol[0].mVal[0] = scale;
    res.mCol[0].mVal[1] = scale;
    res.mCol[0].mVal[2] = scale;
    res.mCol[1].mVal[0] = scale;
    res.mCol[1].mVal[1] = scale;
    res.mCol[1].mVal[2] = scale;
    res.mCol[2].mVal[0] = scale;
    res.mCol[2].mVal[1] = scale;
    res.mCol[2].mVal[2] = scale;
    res.mCol[3].mVal[3] = 1.0f;

    const Mat4 rot = ToMat4(orientation);

    res.mCol[0].mVal[0] *= rot.mCol[0].mVal[0];
    res.mCol[0].mVal[1] *= rot.mCol[0].mVal[1];
    res.mCol[0].mVal[2] *= rot.mCol[0].mVal[2];
    res.mCol[1].mVal[0] *= rot.mCol[1].mVal[0];
    res.mCol[1].mVal[1] *= rot.mCol[1].mVal[1];
    res.mCol[1].mVal[2] *= rot.mCol[1].mVal[2];
    res.mCol[2].mVal[0] *= rot.mCol[2].mVal[0];
    res.mCol[2].mVal[1] *= rot.mCol[2].mVal[1];
    res.mCol[2].mVal[2] *= rot.mCol[2].mVal[2];

    res.mCol[3].mVal[0] = position.mVal[0];
    res.mCol[3].mVal[1] = position.mVal[1];
    res.mCol[3].mVal[2] = position.mVal[2];

    return res;
}

inline constexpr Mat3 ToMat3(const Mat4& m)
{
    // clang-format off
    return {
        m.mCol[0].mVal[0], m.mCol[0].mVal[1], m.mCol[0].mVal[2],
        m.mCol[1].mVal[0], m.mCol[1].mVal[1], m.mCol[1].mVal[2],
        m.mCol[2].mVal[0], m.mCol[2].mVal[1], m.mCol[2].mVal[2],
    };
    // clang-format on
}

// The Laplace Expansion Theorem: Computing the Determinants and Inverses of Matrices
// by David Eberly:
// https://www.geometrictools.com/Documentation/LaplaceExpansionTheorem.pdf
inline Mat4 Inverse(const Mat4& m)
{
    const f32 s0 = m.mCol[0].mVal[0] * m.mCol[1].mVal[1] - m.mCol[0].mVal[1] * m.mCol[1].mVal[0];
    const f32 s1 = m.mCol[0].mVal[0] * m.mCol[2].mVal[1] - m.mCol[0].mVal[1] * m.mCol[2].mVal[0];
    const f32 s2 = m.mCol[0].mVal[0] * m.mCol[3].mVal[1] - m.mCol[0].mVal[1] * m.mCol[3].mVal[0];
    const f32 s3 = m.mCol[1].mVal[0] * m.mCol[2].mVal[1] - m.mCol[1].mVal[1] * m.mCol[2].mVal[0];
    const f32 s4 = m.mCol[1].mVal[0] * m.mCol[3].mVal[1] - m.mCol[1].mVal[1] * m.mCol[3].mVal[0];
    const f32 s5 = m.mCol[2].mVal[0] * m.mCol[3].mVal[1] - m.mCol[2].mVal[1] * m.mCol[3].mVal[0];

    const f32 c5 = m.mCol[2].mVal[2] * m.mCol[3].mVal[3] - m.mCol[2].mVal[3] * m.mCol[3].mVal[2];
    const f32 c4 = m.mCol[1].mVal[2] * m.mCol[3].mVal[3] - m.mCol[1].mVal[3] * m.mCol[3].mVal[2];
    const f32 c3 = m.mCol[1].mVal[2] * m.mCol[2].mVal[3] - m.mCol[1].mVal[3] * m.mCol[2].mVal[2];
    const f32 c2 = m.mCol[0].mVal[2] * m.mCol[3].mVal[3] - m.mCol[0].mVal[3] * m.mCol[3].mVal[2];
    const f32 c1 = m.mCol[0].mVal[2] * m.mCol[2].mVal[3] - m.mCol[0].mVal[3] * m.mCol[2].mVal[2];
    const f32 c0 = m.mCol[0].mVal[2] * m.mCol[1].mVal[3] - m.mCol[0].mVal[3] * m.mCol[1].mVal[2];

    const f32 invDet = 1.0f / (s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0);
    assert(invDet != 0.0f);

    Mat4 result;

    result.mCol[0].mVal[0]
        = (m.mCol[1].mVal[1] * c5 - m.mCol[2].mVal[1] * c4 + m.mCol[3].mVal[1] * c3) * invDet;
    result.mCol[1].mVal[0]
        = (-m.mCol[1].mVal[0] * c5 + m.mCol[2].mVal[0] * c4 - m.mCol[3].mVal[0] * c3) * invDet;
    result.mCol[2].mVal[0]
        = (m.mCol[1].mVal[3] * s5 - m.mCol[2].mVal[3] * s4 + m.mCol[3].mVal[3] * s3) * invDet;
    result.mCol[3].mVal[0]
        = (-m.mCol[1].mVal[2] * s5 + m.mCol[2].mVal[2] * s4 - m.mCol[3].mVal[2] * s3) * invDet;

    result.mCol[0].mVal[1]
        = (-m.mCol[0].mVal[1] * c5 + m.mCol[2].mVal[1] * c2 - m.mCol[3].mVal[1] * c1) * invDet;
    result.mCol[1].mVal[1]
        = (m.mCol[0].mVal[0] * c5 - m.mCol[2].mVal[0] * c2 + m.mCol[3].mVal[0] * c1) * invDet;
    result.mCol[2].mVal[1]
        = (-m.mCol[0].mVal[3] * s5 + m.mCol[2].mVal[3] * s2 - m.mCol[3].mVal[3] * s1) * invDet;
    result.mCol[3].mVal[1]
        = (m.mCol[0].mVal[2] * s5 - m.mCol[2].mVal[2] * s2 + m.mCol[3].mVal[2] * s1) * invDet;

    result.mCol[0].mVal[2]
        = (m.mCol[0].mVal[1] * c4 - m.mCol[1].mVal[1] * c2 + m.mCol[3].mVal[1] * c0) * invDet;
    result.mCol[1].mVal[2]
        = (-m.mCol[0].mVal[0] * c4 + m.mCol[1].mVal[0] * c2 - m.mCol[3].mVal[0] * c0) * invDet;
    result.mCol[2].mVal[2]
        = (m.mCol[0].mVal[3] * s4 - m.mCol[1].mVal[3] * s2 + m.mCol[3].mVal[3] * s0) * invDet;
    result.mCol[3].mVal[2]
        = (-m.mCol[0].mVal[2] * s4 + m.mCol[1].mVal[2] * s2 - m.mCol[3].mVal[2] * s0) * invDet;

    result.mCol[0].mVal[3]
        = (-m.mCol[0].mVal[1] * c3 + m.mCol[1].mVal[1] * c1 - m.mCol[2].mVal[1] * c0) * invDet;
    result.mCol[1].mVal[3]
        = (m.mCol[0].mVal[0] * c3 - m.mCol[1].mVal[0] * c1 + m.mCol[2].mVal[0] * c0) * invDet;
    result.mCol[2].mVal[3]
        = (-m.mCol[0].mVal[3] * s3 + m.mCol[1].mVal[3] * s1 - m.mCol[2].mVal[3] * s0) * invDet;
    result.mCol[3].mVal[3]
        = (m.mCol[0].mVal[2] * s3 - m.mCol[1].mVal[2] * s1 + m.mCol[2].mVal[2] * s0) * invDet;

    return result;
}
