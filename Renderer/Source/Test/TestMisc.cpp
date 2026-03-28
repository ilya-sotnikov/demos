#if defined(TEST_HEADERS)

#include "PackUtils.hpp"

#elif defined(TEST_SOURCE)

TEST("Pack 3 bytes to f32, unpack f32 to Vec3")
{
    constexpr f32 tolerance = 0.005f;

    TEST_ASSERT(AlmostEqual(UnpackToVec3(PackToF32(0, 0, 0)), {0.0f, 0.0f, 0.0f}, tolerance));
    TEST_ASSERT(AlmostEqual(UnpackToVec3(PackToF32(0, 0, 255)), {0.0f, 0.0f, 1.0f}, tolerance));
    TEST_ASSERT(AlmostEqual(UnpackToVec3(PackToF32(0, 255, 0)), {0.0f, 1.0f, 0.0f}, tolerance));
    TEST_ASSERT(AlmostEqual(UnpackToVec3(PackToF32(255, 0, 0)), {1.0f, 0.0f, 0.0f}, tolerance));
    TEST_ASSERT(AlmostEqual(UnpackToVec3(PackToF32(255, 0, 255)), {1.0f, 0.0f, 1.0f}, tolerance));
    TEST_ASSERT(AlmostEqual(UnpackToVec3(PackToF32(255, 255, 0)), {1.0f, 1.0f, 0.0f}, tolerance));
    TEST_ASSERT(AlmostEqual(UnpackToVec3(PackToF32(127, 127, 127)), {0.5f, 0.5f, 0.5f}, tolerance));
    TEST_ASSERT(AlmostEqual(UnpackToVec3(PackToF32(255, 255, 255)), {1.0f, 1.0f, 1.0f}, tolerance));
}

TEST("Normal octahedral encoding")
{
    Vec3 v{};

    v = {1.0f, 0.0f, 0.0f};
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = {0.0f, 1.0f, 0.0f};
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = {0.0f, 0.0f, 1.0f};
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = {-1.0f, 0.0f, 0.0f};
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = {0.0f, -1.0f, 0.0f};
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = {0.0f, 0.0f, -1.0f};
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = Normalize({1.0f, 1.0f, 0.0f});
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = Normalize({0.0f, 1.0f, 1.0f});
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = Normalize({-1.0f, -1.0f, 0.0f});
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = Normalize({0.0f, -1.0f, -1.0f});
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = Normalize({-1.0f, 0.0f, -1.0f});
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = Normalize({1.0f, 0.0f, -1.0f});
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));
    v = Normalize({-1.0f, 1.0f, 0.0f});
    TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v));

    u32 lfsr = 1337;
    for (int i = 0; i < 1000; ++i)
    {
        const f32 tolerance = 1e-6f;
        const f32 r0 = LfsrNextGetFloat(lfsr, 1.0f);
        const f32 r1 = LfsrNextGetFloat(lfsr, 1.0f);
        const f32 r2 = LfsrNextGetFloat(lfsr, 1.0f);
        v = Normalize({r0, r1, r2});
        TEST_ASSERT(AlmostEqual(UnpackNormalOctahedral(PackNormalOctahedral(v)), v, tolerance));
    }
}

#endif
