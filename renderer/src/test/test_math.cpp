#if defined(TEST_HEADERS)

    #include "math/math.hpp"

#elif defined(TEST_SOURCE)

TEST("Math utils") {
    TEST_ASSERT(math::almost_equal(0.0f, 0.0f));
    TEST_ASSERT(math::almost_equal(1.0f, 1.0f));
    TEST_ASSERT(math::almost_equal(-1.0f, -1.0f));
    TEST_ASSERT(math::almost_equal(13.37f, 13.37f));

    TEST_ASSERT(math::almost_equal(math::fract(0.0f), 0.0f));
    TEST_ASSERT(math::almost_equal(math::fract(0.1f), 0.1f));
    TEST_ASSERT(math::almost_equal(math::fract(-1.0f), 0.0f));
    TEST_ASSERT(math::almost_equal(math::fract(-1.1f), -0.1f));
    TEST_ASSERT(math::almost_equal(math::fract(13.37f), 0.37f));
}

TEST("perp_dot") {
    const Vec2 a = {5.0f, 3.0f};
    const Vec2 b = {0.5f, -4.0f};
    TEST_ASSERT(math::almost_equal(math::perp_dot(a, b), -21.5f));
}

TEST("Vec2 operators") {
    const Vec2 v1 = {2.0f, 3.0f};
    const Vec2 v2 = {10.0f, 11.0f};
    Vec2 res{};
    Vec2 cmp{};

    TEST_ASSERT(v1 == v1);
    TEST_ASSERT(v1 != v2);

    res = -v2;
    cmp = {-10.0f, -11.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res += v2;
    cmp = {12.0f, 14.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 + v2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res -= v2;
    cmp = {-8.0f, -8.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 - v2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res *= v2;
    cmp = {20.0f, 33.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 * v2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res *= 2.0f;
    cmp = {4.0f, 6.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 * 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = 2.0f * v1;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res /= 2.0f;
    cmp = {1.0f, 1.5f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 / 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Vec3 operators") {
    const Vec3 v1 = {2.0f, 3.0f, 4.0f};
    const Vec3 v2 = {10.0f, 11.0f, 5.0f};
    Vec3 res{};
    Vec3 cmp{};

    TEST_ASSERT(v1 == v1);
    TEST_ASSERT(v1 != v2);

    res = -v2;
    cmp = {-10.0f, -11.0f, -5.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res += v2;
    cmp = {12.0f, 14.0f, 9.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 + v2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res -= v2;
    cmp = {-8.0f, -8.0f, -1.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 - v2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res *= v2;
    cmp = {20.0f, 33.0f, 20.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 * v2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res *= 2.0f;
    cmp = {4.0f, 6.0f, 8.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 * 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = 2.0f * v1;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res /= 2.0f;
    cmp = {1.0f, 1.5f, 2.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 / 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Vec4 operators") {
    const Vec4 v1 = {2.0f, 3.0f, 4.0f, -2.0f};
    const Vec4 v2 = {10.0f, 11.0f, 5.0f, 1.0f};
    Vec4 res{};
    Vec4 cmp{};

    TEST_ASSERT(v1 == v1);
    TEST_ASSERT(v1 != v2);

    res = -v2;
    cmp = {-10.0f, -11.0f, -5.0f, -1.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res += v2;
    cmp = {12.0f, 14.0f, 9.0f, -1.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 + v2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res -= v2;
    cmp = {-8.0f, -8.0f, -1.0f, -3.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 - v2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res *= v2;
    cmp = {20.0f, 33.0f, 20.0f, -2.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 * v2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res *= 2.0f;
    cmp = {4.0f, 6.0f, 8.0f, -4.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 * 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = 2.0f * v1;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v1;
    res /= 2.0f;
    cmp = {1.0f, 1.5f, 2.0f, -1.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = v1 / 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Transpose Mat2") {
    const Mat2 m = {5.0f, 15.0f, -2.0f, 4.0f};
    const Mat2 res = math::transpose(m);
    const Mat2 cmp = {5.0f, -2.0f, 15.0f, 4.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Mat2 operators") {
    const Mat2 m1 = {2.0f, 3.0f, 4.0f, 5.0f};
    const Mat2 m2 = {4.0f, -3.0f, -2.0f, -3.0f};
    Mat2 res{};
    Mat2 cmp{};

    TEST_ASSERT(m1 == m1);
    TEST_ASSERT(m1 != m2);

    res = -m1;
    cmp = {-2.0f, -3.0f, -4.0f, -5.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res += m2;
    cmp = {6.0f, 0.0f, 2.0f, 2.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 + m2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res -= m2;
    cmp = {-2.0f, 6.0f, 6.0f, 8.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 - m2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res *= 2.0f;
    cmp = {4.0f, 6.0f, 8.0f, 10.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 * 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = 2.0f * m1;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res /= 2.0f;
    cmp = {1.0f, 1.5f, 2.0f, 2.5f};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 / 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));

    {
        const Vec2 v = {2.0f, -3.0f};
        Vec2 v_res{};
        Vec2 v_cmp{};

        v_res = m1 * v;
        v_cmp = {-8.0f, -9.0f};
        TEST_ASSERT(math::almost_equal(v_res, v_cmp));

        v_res = math::tmul(m1, v);
        v_cmp = math::transpose(m1) * v;
        TEST_ASSERT(math::almost_equal(v_res, v_cmp));
    }

    res = m1 * m2;
    cmp = {-4.0f, -3.0f, -16.0f, -21.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = math::tmul(m1, m2);
    cmp = math::transpose(m1) * m2;
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Transpose Mat3") {
    // clang-format off
    const Mat3 m = {
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f,
        7.0f, 8.0f, 9.0f
    };
    const Mat3 cmp = {
        1.0f, 4.0f, 7.0f,
        2.0f, 5.0f, 8.0f,
        3.0f, 6.0f, 9.0f
    };
    // clang-format on

    const Mat3 res = math::transpose(m);
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Mat3 operators") {
    const Mat3 m1 = {Vec3{2.0f, 3.0f, 4.0f}, Vec3{5.0f, 6.0f, 7.0f}, Vec3{8.0f, 9.0f, 10.0f}};
    const Mat3 m2 = {Vec3{4.0f, -3.0f, -2.0f}, Vec3{5.0f, 3.0f, 2.0f}, Vec3{1.0f, 2.0f, 3.0f}};
    Mat3 res{};
    Mat3 cmp{};

    TEST_ASSERT(m1 == m1);
    TEST_ASSERT(m1 != m2);

    res = -m1;
    cmp = {Vec3{-2.0f, -3.0f, -4.0f}, Vec3{-5.0f, -6.0f, -7.0f}, Vec3{-8.0f, -9.0f, -10.0f}};
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res += m2;
    cmp = {Vec3{6.0f, 0.0f, 2.0f}, Vec3{10.0f, 9.0f, 9.0f}, Vec3{9.0f, 11.0f, 13.0f}};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 + m2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res -= m2;
    cmp = {Vec3{-2.0f, 6.0f, 6.0f}, Vec3{0.0f, 3.0f, 5.0f}, Vec3{7.0f, 7.0f, 7.0f}};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 - m2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res *= 2.0f;
    cmp = {Vec3{4.0f, 6.0f, 8.0f}, Vec3{10.0f, 12.0f, 14.0f}, Vec3{16.0f, 18.0f, 20.0f}};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 * 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = 2.0f * m1;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res /= 2.0f;
    cmp = {Vec3{1.0f, 1.5f, 2.0f}, Vec3{2.5f, 3.0f, 3.5f}, Vec3{4.0f, 4.5f, 5.0f}};
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 / 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));

    {
        const Vec3 v = {2.0f, -3.0f, 5.0f};
        Vec3 v_res{};
        Vec3 v_cmp{};

        v_res = m1 * v;
        v_cmp = {29.0f, 33.0f, 37.0f};
        TEST_ASSERT(math::almost_equal(v_res, v_cmp));

        v_res = math::tmul(m1, v);
        v_cmp = math::transpose(m1) * v;
        TEST_ASSERT(math::almost_equal(v_res, v_cmp));
    }

    res = m1 * m2;
    cmp = {Vec3{-23.0f, -24.0f, -25.0f}, Vec3{41.0f, 51.0f, 61.0f}, Vec3{36.0f, 42.0f, 48.0f}};
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = math::tmul(m1, m2);
    cmp = math::transpose(m1) * m2;
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Transpose Mat4") {
    // clang-format off
    const Mat4 m = {
        2.0f, 3.0f, 4.0f, 5.0f,
        5.0f, 6.0f, 7.0f, 2.0f,
        8.0f, 9.0f, 10.0f, -1.0f,
        5.0f, -4.0f, 3.0f, 2.0f
    };
    const Mat4 cmp = {
        2.0f, 5.0f, 8.0f, 5.0f,
        3.0f, 6.0f, 9.0f, -4.0f,
        4.0f, 7.0f, 10.0f, 3.0f,
        5.0f, 2.0f, -1.0f, 2.0f
    };
    // clang-format on

    const Mat4 res = math::transpose(m);
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Mat4 operators") {
    const Mat4 m1 = {
        Vec4{2.0f, 3.0f, 4.0f, 5.0f},
        Vec4{5.0f, 6.0f, 7.0f, 2.0f},
        Vec4{8.0f, 9.0f, 10.0f, -1.0f},
        Vec4{5.0f, -4.0f, 3.0f, 2.0f}
    };
    const Mat4 m2 = {
        Vec4{4.0f, -3.0f, -2.0f, -1.0f},
        Vec4{5.0f, 3.0f, 2.0f, -3.0f},
        Vec4{1.0f, 2.0f, 3.0f, 4.0f},
        Vec4{6.0f, 2.0f, -3.0f, -1.0f}
    };
    Mat4 res{};
    Mat4 cmp{};

    TEST_ASSERT(m1 == m1);
    TEST_ASSERT(m1 != m2);

    res = -m1;
    cmp = {
        Vec4{-2.0f, -3.0f, -4.0f, -5.0f},
        Vec4{-5.0f, -6.0f, -7.0f, -2.0f},
        Vec4{-8.0f, -9.0f, -10.0f, 1.0f},
        Vec4{-5.0f, 4.0f, -3.0f, -2.0f}
    };
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res += m2;
    cmp = {
        Vec4{6.0f, 0.0f, 2.0f, 4.0f},
        Vec4{10.0f, 9.0f, 9.0f, -1.0f},
        Vec4{9.0f, 11.0f, 13.0f, 3.0f},
        Vec4{11.0f, -2.0f, 0.0f, 1.0f}
    };
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 + m2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res -= m2;
    cmp = {
        Vec4{-2.0f, 6.0f, 6.0f, 6.0f},
        Vec4{0.0f, 3.0f, 5.0f, 5.0f},
        Vec4{7.0f, 7.0f, 7.0f, -5.0f},
        Vec4{-1.0f, -6.0f, 6.0f, 3.0f}
    };
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 - m2;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res *= 2.0f;
    cmp = {
        Vec4{4.0f, 6.0f, 8.0f, 10.0f},
        Vec4{10.0f, 12.0f, 14.0f, 4.0f},
        Vec4{16.0f, 18.0f, 20.0f, -2.0f},
        Vec4{10.0f, -8.0f, 6.0f, 4.0f}
    };
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 * 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = 2.0f * m1;
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = m1;
    res /= 2.0f;
    cmp = {
        Vec4{1.0f, 1.5f, 2.0f, 2.5f},
        Vec4{2.5f, 3.0f, 3.5f, 1.0f},
        Vec4{4.0f, 4.5f, 5.0f, -0.5f},
        Vec4{2.5f, -2.0f, 1.5f, 1.0f}
    };
    TEST_ASSERT(math::almost_equal(res, cmp));
    res = m1 / 2.0f;
    TEST_ASSERT(math::almost_equal(res, cmp));

    {
        const Vec4 v = {2.0f, -3.0f, 5.0f, -1.0f};
        Vec4 v_res{};
        Vec4 v_cmp{};

        v_res = m1 * v;
        v_cmp = {24.0f, 37.0f, 34.0f, -3.0f};
        TEST_ASSERT(math::almost_equal(v_res, v_cmp));

        v_res = math::tmul(m1, v);
        v_cmp = math::transpose(m1) * v;
        TEST_ASSERT(math::almost_equal(v_res, v_cmp));
    }

    res = m1 * m2;
    cmp = {
        Vec4{-28.0f, -20.0f, -28.0f, 14.0f},
        Vec4{26.0f, 63.0f, 52.0f, 23.0f},
        Vec4{56.0f, 26.0f, 60.0f, 14.0f},
        Vec4{-7.0f, 7.0f, 5.0f, 35.0f}
    };
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = math::tmul(m1, m2);
    cmp = math::transpose(m1) * m2;
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Determinant Mat2") {
    const Mat2 m = {5.0f, 15.0f, -2.0f, 4.0f};
    TEST_ASSERT(math::almost_equal(math::determinant(m), 50.0f));
}

TEST("Determinant Mat3") {
    const Mat3 m = {5.0f, 3.0f, 6.0f, -2.0f, 1.0f, 7.0f, 1.0f, -4.0f, -3.0f};
    TEST_ASSERT(math::almost_equal(math::determinant(m), 170.0f));
}

TEST("Inverse Mat2") {
    const Mat2 m = {5.0f, 15.0f, -2.0f, 4.0f};
    const Mat2 res = math::inverse(m);
    const Mat2 cmp = 1.0f / 50.0f * Mat2{4.0f, 2.0f, -15.0f, 5.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Inverse Mat3") {
    const Mat3 m = {5.0f, 3.0f, 6.0f, -2.0f, 1.0f, 7.0f, 1.0f, -4.0f, -3.0f};
    const Mat3 res = math::inverse(m);
    const Mat3 cmp =
        1.0f / 170.0f * Mat3{25.0f, -15.0f, 15.0f, 1.0f, -21.0f, -47.0f, 7.0f, 23.0f, 11.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Inverse Mat4") {
    // clang-format off
    const Mat4 m = {
        2.0f, 3.0f, 4.0f, 5.0f,
        5.0f, 6.0f, 7.0f, 2.0f,
        8.0f, 8.0f, 10.0f, -1.0f,
        5.0f, -4.0f, 3.0f, 2.0f
    };
    const Mat4 cmp = (1.0f / 84.0f) * Mat4{
        -182.0f, 341.0f, -174.0f, 27.0f,
        -84.0f, 168.0f, -84.0f, 0.0f,
        210.0f, -399.0f, 210.0f, -21.0f,
        -28.0f, 82.0f, -48.0f, 6.0f
    };
    // clang-format on

    const Mat4 res = math::inverse(m);
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Quat mul") {
    const Quat q1 = {1.0f, -2.0f, 3.0f, 4.0f};
    const Quat q2 = {5.0f, 3.0f, 8.0f, 7.0f};
    Quat res{};
    Quat cmp{};
    res = q1 * q2;
    cmp = {-41.0f, -18.0f, 49.0f, 2.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));

    const Vec3 v = {3.0f, 8.0f, 7.0f};
    res = q1 * v;
    cmp = {-46.0f, -8.0f, 34.0f, -18.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));

    res = v * q1;
    cmp = {-46.0f, 14.0f, -18.0f, 32.0f};
    TEST_ASSERT(math::almost_equal(res, cmp));
}

TEST("Model matrix") {
    const Vec3 position = {5.0f, -2.0f, 3.0f};
    const Quat orientation = Quat::from_axis(math::radians(20.0f), 0.0f, 0.0f, 1.0f);
    const Vec3 scale = {2.0f, 3.0f, 4.0f};

    Mat4 model = math::scale(
        math::translate(Mat4::identity(), position) * math::to_mat4(orientation),
        scale
    );
    TEST_ASSERT(math::almost_equal(model, math::model(position, orientation, scale)));

    model =
        math::scale(math::translate(Mat4::identity(), position) * math::to_mat4(orientation), 2.0f);
    TEST_ASSERT(math::almost_equal(model, math::model(position, orientation, 2.0f)));
}

#endif
