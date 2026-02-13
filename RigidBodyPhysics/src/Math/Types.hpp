#pragma once

#include "../Common.hpp"

struct [[nodiscard]] Vec2
{
    static constexpr int N = 2;

    f32 mVal[N];

    Vec2() = default;
    constexpr Vec2(f32 x, f32 y) : mVal{x, y} { }
    explicit constexpr Vec2(f32 fill) : mVal{fill, fill} { }

    // NOTE: could've used a union, it's UB in ISO C++ (fine in C),
    // but every major compiler doesn't break the code, gcc even explicitly
    // allows it as an compiler extensions (maybe other compilers too, idk).
    // Many codebases do type-punning through unions, so at this point if
    // some compiler breaks the code a bug report would be appropriate.
    // For now I chose to use accessor methods, kinda ugly, but tolerable.
    [[nodiscard]]
    constexpr f32 X() const;
    [[nodiscard]]
    constexpr f32 Y() const;

    [[nodiscard]]
    constexpr f32& X();
    [[nodiscard]]
    constexpr f32& Y();

    [[nodiscard]]
    constexpr f32 operator[](int i) const;
    [[nodiscard]]
    constexpr f32& operator[](int i);
};

struct [[nodiscard]] Vec3
{
    static constexpr int N = 3;

    f32 mVal[N];

    Vec3() = default;
    constexpr Vec3(f32 x, f32 y, f32 z) : mVal{x, y, z} { }
    constexpr Vec3(Vec2 xy, f32 z) : mVal{xy.mVal[0], xy.mVal[1], z} { }
    explicit constexpr Vec3(f32 fill) : mVal{fill, fill, fill} { }

    [[nodiscard]]
    constexpr f32 X() const;
    [[nodiscard]]
    constexpr f32 Y() const;
    [[nodiscard]]
    constexpr f32 Z() const;
    [[nodiscard]]
    constexpr f32 R() const;
    [[nodiscard]]
    constexpr f32 G() const;
    [[nodiscard]]
    constexpr f32 B() const;

    [[nodiscard]]
    constexpr f32& X();
    [[nodiscard]]
    constexpr f32& Y();
    [[nodiscard]]
    constexpr f32& Z();
    [[nodiscard]]
    constexpr f32& R();
    [[nodiscard]]
    constexpr f32& G();
    [[nodiscard]]
    constexpr f32& B();

    [[nodiscard]]
    constexpr f32 operator[](int i) const;
    [[nodiscard]]
    constexpr f32& operator[](int i);
};

struct [[nodiscard]] Vec4
{
    static constexpr int N = 4;

    f32 mVal[N];

    Vec4() = default;
    constexpr Vec4(f32 x, f32 y, f32 z, f32 w) : mVal{x, y, z, w} { }
    constexpr Vec4(Vec3 xyz, f32 w) : mVal{xyz.mVal[0], xyz.mVal[1], xyz.mVal[2], w} { }
    explicit constexpr Vec4(f32 fill) : mVal{fill, fill, fill, fill} { }

    [[nodiscard]]
    constexpr f32 X() const;
    [[nodiscard]]
    constexpr f32 Y() const;
    [[nodiscard]]
    constexpr f32 Z() const;
    [[nodiscard]]
    constexpr f32 W() const;
    [[nodiscard]]
    constexpr f32 R() const;
    [[nodiscard]]
    constexpr f32 G() const;
    [[nodiscard]]
    constexpr f32 B() const;
    [[nodiscard]]
    constexpr f32 A() const;

    [[nodiscard]]
    constexpr f32& X();
    [[nodiscard]]
    constexpr f32& Y();
    [[nodiscard]]
    constexpr f32& Z();
    [[nodiscard]]
    constexpr f32& W();
    [[nodiscard]]
    constexpr f32& R();
    [[nodiscard]]
    constexpr f32& G();
    [[nodiscard]]
    constexpr f32& B();
    [[nodiscard]]
    constexpr f32& A();

    [[nodiscard]]
    constexpr Vec2 XY() const;
    [[nodiscard]]
    constexpr Vec3 XYZ() const;
    [[nodiscard]]
    constexpr Vec3 RGB() const;

    [[nodiscard]]
    constexpr f32 operator[](int i) const;
    [[nodiscard]]
    constexpr f32& operator[](int i);
};

struct [[nodiscard]] Quat
{
    static constexpr int N = 4;

    f32 mVal[N];

    Quat() = default;
    constexpr Quat(f32 w, f32 x, f32 y, f32 z) : mVal{w, x, y, z} { }
    explicit constexpr Quat(f32 fill) : mVal{fill, fill, fill, fill} { }

    [[nodiscard]]
    constexpr f32 W() const;
    [[nodiscard]]
    constexpr f32 X() const;
    [[nodiscard]]
    constexpr f32 Y() const;
    [[nodiscard]]
    constexpr f32 Z() const;
    [[nodiscard]]
    constexpr f32& W();
    [[nodiscard]]
    constexpr f32& X();
    [[nodiscard]]
    constexpr f32& Y();
    [[nodiscard]]
    constexpr f32& Z();

    [[nodiscard]]
    static Quat FromAxis(f32 rad, f32 x, f32 y, f32 z);
    [[nodiscard]]
    static Quat FromAxis(f32 rad, Vec3 axis);

    [[nodiscard]]
    constexpr f32 operator[](int i) const;
    [[nodiscard]]
    constexpr f32& operator[](int i);
};

// All matrices are in column-major order.
// With post-multiplication (Mat4 * vec4).
// Translation component is in 12, 13, 14.

// 0  2
// 1  3
struct [[nodiscard]] Mat2
{
    static constexpr int N = 2;

    Vec2 mCol[N];

    Mat2() = default;
    constexpr Mat2(f32 x0, f32 x1, f32 x2, f32 x3) : mCol{{x0, x1}, {x2, x3}} { }
    constexpr Mat2(Vec2 col0, Vec2 col1) : mCol{col0, col1} { }
    explicit constexpr Mat2(f32 fill) : mCol{Vec2{fill}, Vec2{fill}} { }

    [[nodiscard]]
    static constexpr Mat2 Identity();
    [[nodiscard]]
    static constexpr Mat2 Zero();
    [[nodiscard]]
    static constexpr Mat2 FromAngle(f32 angle);

    [[nodiscard]]
    constexpr f32 operator()(int row, int col) const;
    [[nodiscard]]
    constexpr f32& operator()(int row, int col);
};

// 0  3  6
// 1  4  7
// 2  5  8
struct [[nodiscard]] Mat3
{
    static constexpr int N = 3;

    Vec3 mCol[N];

    Mat3() = default;
    // clang-format off
    constexpr Mat3(
        f32 x0, f32 x1, f32 x2, f32 x3, f32 x4, f32 x5, f32 x6, f32 x7, f32 x8
    )
        : mCol{Vec3{x0, x1, x2}, Vec3{x3, x4, x5}, Vec3{x6, x7, x8}}
    { }
    // clang-format on
    constexpr Mat3(Vec3 col0, Vec3 col1, Vec3 col2) : mCol{col0, col1, col2} { }
    explicit constexpr Mat3(f32 fill) : mCol{Vec3{fill}, Vec3{fill}, Vec3{fill}} { }

    [[nodiscard]]
    static constexpr Mat3 Identity();
    [[nodiscard]]
    static constexpr Mat3 Zero();

    [[nodiscard]]
    constexpr f32 operator()(int row, int col) const;
    [[nodiscard]]
    constexpr f32& operator()(int row, int col);
};

// 0  4  8  12
// 1  5  9  13
// 2  6  10 14
// 3  7  11 15
struct [[nodiscard]] Mat4
{
    static constexpr int N = 4;

    Vec4 mCol[N];

    Mat4() = default;
    // clang-format off
    constexpr Mat4(f32 v0, f32 v1, f32 v2, f32 v3, f32 v4, f32 v5, f32 v6, f32 v7,
                   f32 v8, f32 v9, f32 v10, f32 v11, f32 v12, f32 v13, f32 v14, f32 v15)
        : mCol{Vec4{v0, v1, v2, v3}, Vec4{v4, v5, v6, v7},
              Vec4{v8, v9, v10, v11}, Vec4{v12, v13, v14, v15}}
    { }
    // clang-format on
    constexpr Mat4(Vec4 col0, Vec4 col1, Vec4 col2, Vec4 col3) : mCol{col0, col1, col2, col3} { }
    explicit constexpr Mat4(f32 fill) : mCol{Vec4{fill}, Vec4{fill}, Vec4{fill}, Vec4{fill}} { }

    [[nodiscard]]
    static constexpr Mat4 Identity();
    [[nodiscard]]
    static constexpr Mat4 Zero();

    [[nodiscard]]
    constexpr f32 operator()(int row, int col) const;
    [[nodiscard]]
    constexpr f32& operator()(int row, int col);
};

static constexpr Vec3 WORLD_X = {1.0f, 0.0f, 0.0f};
static constexpr Vec3 WORLD_Y = {0.0f, 1.0f, 0.0f};
static constexpr Vec3 WORLD_Z = {0.0f, 0.0f, 1.0f};
