#pragma once

#include "../Common.hpp"

using Half2 = f16[2];
using Half3 = f16[3];
using Half4 = f16[4];

struct Vec2
{
    static constexpr int N = 2;

    f32 val[N];

    Vec2() = default;
    Vec2(f32 x, f32 y) : val{x, y} { }
    explicit Vec2(f32 fill) : val{fill, fill} { }

    // NOTE: could've used a union, it's UB in ISO C++ (fine in C),
    // but every major compiler doesn't break the code, gcc even explicitly
    // allows it as an compiler extensions (maybe other compilers too, idk).
    // Many codebases do type-punning through unions, so at this point if
    // some compiler breaks the code a bug report would be appropriate.
    // For now I chose to use accessor methods, kinda ugly, but tolerable.

    f32 X() const;
    f32 Y() const;
    f32& X();
    f32& Y();

    f32 operator[](int i) const;
    f32& operator[](int i);
};

struct Vec3
{
    constexpr static int N = 3;

    f32 val[N];

    Vec3() = default;
    Vec3(f32 x, f32 y, f32 z) : val{x, y, z} { }
    Vec3(Vec2 xy, f32 z) : val{xy.val[0], xy.val[1], z} { }
    explicit Vec3(f32 fill) : val{fill, fill, fill} { }

    f32 X() const;
    f32 Y() const;
    f32 Z() const;
    f32 R() const;
    f32 G() const;
    f32 B() const;

    f32& X();
    f32& Y();
    f32& Z();
    f32& R();
    f32& G();
    f32& B();

    f32 operator[](int i) const;
    f32& operator[](int i);
};

struct Vec4
{
    static constexpr int N = 4;

    f32 val[N];

    Vec4() = default;
    Vec4(f32 x, f32 y, f32 z, f32 w) : val{x, y, z, w} { }
    Vec4(Vec3 xyz, f32 w) : val{xyz.val[0], xyz.val[1], xyz.val[2], w} { }
    explicit Vec4(f32 fill) : val{fill, fill, fill, fill} { }

    f32 X() const;
    f32 Y() const;
    f32 Z() const;
    f32 W() const;
    f32 R() const;
    f32 G() const;
    f32 B() const;
    f32 A() const;

    f32& X();
    f32& Y();
    f32& Z();
    f32& W();
    f32& R();
    f32& G();
    f32& B();
    f32& A();

    Vec2 XY() const;
    Vec3 XYZ() const;
    Vec3 RGB() const;

    f32 operator[](int i) const;
    f32& operator[](int i);
};

struct Quat
{
    static constexpr int N = 4;

    f32 val[N];

    Quat() = default;
    Quat(f32 w, f32 x, f32 y, f32 z) : val{w, x, y, z} { }
    explicit Quat(f32 fill) : val{fill, fill, fill, fill} { }

    f32 W() const;
    f32 X() const;
    f32 Y() const;
    f32 Z() const;

    f32& W();
    f32& X();
    f32& Y();
    f32& Z();

    static Quat FromAxis(f32 rad, f32 x, f32 y, f32 z);
    static Quat FromAxis(f32 rad, Vec3 axis);

    f32 operator[](int i) const;
    f32& operator[](int i);
};

// All matrices are in column-major order.
// With post-multiplication (Mat4 * vec4).
// Translation component is in 12, 13, 14.

// 0  2
// 1  3
struct Mat2
{
    static constexpr int N = 2;

    Vec2 col[N];

    Mat2() = default;
    Mat2(f32 x0, f32 x1, f32 x2, f32 x3) : col{{x0, x1}, {x2, x3}} { }
    Mat2(Vec2 col0, Vec2 col1) : col{col0, col1} { }
    explicit Mat2(f32 fill) : col{Vec2{fill}, Vec2{fill}} { }

    static Mat2 Identity();
    static Mat2 Zero();
    static Mat2 FromAngle(f32 angle);

    f32 operator()(int row, int column) const;
    f32& operator()(int row, int column);
};

// 0  3  6
// 1  4  7
// 2  5  8
struct Mat3
{
    static constexpr int N = 3;

    Vec3 col[N];

    Mat3() = default;
    Mat3(f32 x0, f32 x1, f32 x2, f32 x3, f32 x4, f32 x5, f32 x6, f32 x7, f32 x8)
        : col{Vec3{x0, x1, x2}, Vec3{x3, x4, x5}, Vec3{x6, x7, x8}}
    { }
    Mat3(Vec3 col0, Vec3 col1, Vec3 col2) : col{col0, col1, col2} { }
    explicit Mat3(f32 fill) : col{Vec3{fill}, Vec3{fill}, Vec3{fill}} { }

    static Mat3 Identity();
    static Mat3 Zero();

    f32 operator()(int row, int column) const;
    f32& operator()(int row, int column);
};

// 0  4  8  12
// 1  5  9  13
// 2  6  10 14
// 3  7  11 15
struct Mat4
{
    static constexpr int N = 4;

    Vec4 col[N];

    Mat4() = default;
    // clang-format off
     Mat4(f32 v0, f32 v1, f32 v2, f32 v3, f32 v4, f32 v5, f32 v6, f32 v7,
          f32 v8, f32 v9, f32 v10, f32 v11, f32 v12, f32 v13, f32 v14, f32 v15)
        : col{Vec4{v0, v1, v2, v3}, Vec4{v4, v5, v6, v7},
              Vec4{v8, v9, v10, v11}, Vec4{v12, v13, v14, v15}}
    { }
    // clang-format on
    Mat4(Vec4 col0, Vec4 col1, Vec4 col2, Vec4 col3) : col{col0, col1, col2, col3} { }
    explicit Mat4(f32 fill) : col{Vec4{fill}, Vec4{fill}, Vec4{fill}, Vec4{fill}} { }

    static Mat4 Identity();
    static Mat4 Zero();

    f32 operator()(int row, int column) const;
    f32& operator()(int row, int column);
};

static Vec3 WORLD_X = {1.0f, 0.0f, 0.0f};
static Vec3 WORLD_Y = {0.0f, 1.0f, 0.0f};
static Vec3 WORLD_Z = {0.0f, 0.0f, 1.0f};
