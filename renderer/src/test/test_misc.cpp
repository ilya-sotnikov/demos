#if defined(TEST_HEADERS)

    #include "pack_utils.hpp"

#elif defined(TEST_SOURCE)

TEST("Pack 3 bytes to f32, unpack f32 to Vec3") {
    constexpr f32 tolerance = 0.005f;

    TEST_ASSERT(
        math::almost_equal(unpack_to_vec3(pack_to_f32(0, 0, 0)), {0.0f, 0.0f, 0.0f}, tolerance)
    );
    TEST_ASSERT(
        math::almost_equal(unpack_to_vec3(pack_to_f32(0, 0, 255)), {0.0f, 0.0f, 1.0f}, tolerance)
    );
    TEST_ASSERT(
        math::almost_equal(unpack_to_vec3(pack_to_f32(0, 255, 0)), {0.0f, 1.0f, 0.0f}, tolerance)
    );
    TEST_ASSERT(
        math::almost_equal(unpack_to_vec3(pack_to_f32(255, 0, 0)), {1.0f, 0.0f, 0.0f}, tolerance)
    );
    TEST_ASSERT(
        math::almost_equal(unpack_to_vec3(pack_to_f32(255, 0, 255)), {1.0f, 0.0f, 1.0f}, tolerance)
    );
    TEST_ASSERT(
        math::almost_equal(unpack_to_vec3(pack_to_f32(255, 255, 0)), {1.0f, 1.0f, 0.0f}, tolerance)
    );
    TEST_ASSERT(
        math::almost_equal(
            unpack_to_vec3(pack_to_f32(127, 127, 127)),
            {0.5f, 0.5f, 0.5f},
            tolerance
        )
    );
    TEST_ASSERT(
        math::almost_equal(
            unpack_to_vec3(pack_to_f32(255, 255, 255)),
            {1.0f, 1.0f, 1.0f},
            tolerance
        )
    );
}

TEST("Normal octahedral encoding") {
    Vec3 v{};

    v = {1.0f, 0.0f, 0.0f};
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = {0.0f, 1.0f, 0.0f};
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = {0.0f, 0.0f, 1.0f};
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = {-1.0f, 0.0f, 0.0f};
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = {0.0f, -1.0f, 0.0f};
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = {0.0f, 0.0f, -1.0f};
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = math::normalize({1.0f, 1.0f, 0.0f});
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = math::normalize({0.0f, 1.0f, 1.0f});
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = math::normalize({-1.0f, -1.0f, 0.0f});
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = math::normalize({0.0f, -1.0f, -1.0f});
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = math::normalize({-1.0f, 0.0f, -1.0f});
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = math::normalize({1.0f, 0.0f, -1.0f});
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));
    v = math::normalize({-1.0f, 1.0f, 0.0f});
    TEST_ASSERT(math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v));

    u32 lfsr = 1337;
    for (int i = 0; i < 1000; ++i) {
        const f32 tolerance = 1e-6f;
        const f32 r0 = math::lfsr_next_get_float(lfsr, 1.0f);
        const f32 r1 = math::lfsr_next_get_float(lfsr, 1.0f);
        const f32 r2 = math::lfsr_next_get_float(lfsr, 1.0f);
        v = math::normalize({r0, r1, r2});
        TEST_ASSERT(
            math::almost_equal(unpack_normal_octahedral(pack_normal_octahedral(v)), v, tolerance)
        );
    }
}

TEST("Snorm packing") {
    Vec2 v{};

    v = {0.0f, 0.0f};
    TEST_ASSERT(math::almost_equal(unpack_rg8snorm_to_float2(pack_float2_to_rg8snorm(v)), v));
    v = {1.0f, 0.0f};
    TEST_ASSERT(math::almost_equal(unpack_rg8snorm_to_float2(pack_float2_to_rg8snorm(v)), v));
    v = {0.0f, 1.0f};
    TEST_ASSERT(math::almost_equal(unpack_rg8snorm_to_float2(pack_float2_to_rg8snorm(v)), v));
    v = {1.0f, 1.0f};
    TEST_ASSERT(math::almost_equal(unpack_rg8snorm_to_float2(pack_float2_to_rg8snorm(v)), v));
    v = {0.0f, -1.0f};
    TEST_ASSERT(math::almost_equal(unpack_rg8snorm_to_float2(pack_float2_to_rg8snorm(v)), v));
    v = {-1.0f, 0.0f};
    TEST_ASSERT(math::almost_equal(unpack_rg8snorm_to_float2(pack_float2_to_rg8snorm(v)), v));
    v = {-1.0f, -1.0f};
    TEST_ASSERT(math::almost_equal(unpack_rg8snorm_to_float2(pack_float2_to_rg8snorm(v)), v));

    u32 lfsr = 1337;
    for (int i = 0; i < 1000; ++i) {
        const f32 tolerance = 1e-2f;
        const f32 v0 = math::lfsr_next_get_float(lfsr, 1.0f);
        const f32 v1 = math::lfsr_next_get_float(lfsr, 1.0f);
        v = {v0, v1};
        TEST_ASSERT(
            math::almost_equal(unpack_rg8snorm_to_float2(pack_float2_to_rg8snorm(v)), v, tolerance)
        );
    }
}

#endif
