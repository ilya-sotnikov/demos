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

#endif
