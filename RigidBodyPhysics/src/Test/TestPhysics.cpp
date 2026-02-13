#if defined(TEST_HEADERS)

#include "../Renderer/Meshes.hpp"
#include "../Physics/MassProperties.hpp"
#include "../Arena.hpp"

#elif defined(TEST_SOURCE)

TEST("Mass properties of shapes")
{
    Arena arena{};
    arena.Init(1'000'000);
    Slice<Vec3> positions;
    Slice<Vec3> normals;
    Slice<u16> indices;
    DEFER(arena.FreeBuffer());

    GetCubeData(positions, indices, &normals, arena);

    Mat3 inverseInertiaRes{};
    Mat3 inverseInertiaCmp{};
    f32 inverseMassRes{};
    f32 inverseMassCmp{};
    Vec3 centerOfMass{};

    MassProperties::CalculatePolyhedronTriangleMesh(
        positions.mData,
        indices.mData,
        indices.mCount,
        1.0f,
        Vec3{1.0f},
        inverseInertiaRes,
        centerOfMass,
        inverseMassRes
    );
    MassProperties::CalculateRectangularCuboid(Vec3{1.0f}, 1.0f, inverseInertiaCmp, inverseMassCmp);
    TEST_ASSERT(AlmostEqual(inverseMassRes, inverseMassCmp));
    TEST_ASSERT(AlmostEqual(inverseInertiaRes, inverseInertiaCmp));
    TEST_ASSERT(AlmostEqual(centerOfMass, Vec3{0.0f}));

    f32 density = 1337.0f;
    MassProperties::CalculatePolyhedronTriangleMesh(
        positions.mData,
        indices.mData,
        indices.mCount,
        density,
        Vec3{1.0f},
        inverseInertiaRes,
        centerOfMass,
        inverseMassRes
    );
    MassProperties::CalculateRectangularCuboid(
        Vec3{1.0f},
        density,
        inverseInertiaCmp,
        inverseMassCmp
    );
    TEST_ASSERT(AlmostEqual(inverseMassRes, inverseMassCmp));
    TEST_ASSERT(AlmostEqual(inverseInertiaRes, inverseInertiaCmp));
    TEST_ASSERT(AlmostEqual(centerOfMass, Vec3{0.0f}));

    Vec3 size = {1.0f, 2.0f, 3.0f};
    MassProperties::CalculatePolyhedronTriangleMesh(
        positions.mData,
        indices.mData,
        indices.mCount,
        1.0f,
        size,
        inverseInertiaRes,
        centerOfMass,
        inverseMassRes
    );
    MassProperties::CalculateRectangularCuboid(size, 1.0f, inverseInertiaCmp, inverseMassCmp);
    TEST_ASSERT(AlmostEqual(inverseMassRes, inverseMassCmp));
    TEST_ASSERT(AlmostEqual(inverseInertiaRes, inverseInertiaCmp));
    TEST_ASSERT(AlmostEqual(centerOfMass, Vec3{0.0f}));

    size = {5.0f, 0.5f, 2.0f};
    MassProperties::CalculatePolyhedronTriangleMesh(
        positions.mData,
        indices.mData,
        indices.mCount,
        1.0f,
        size,
        inverseInertiaRes,
        centerOfMass,
        inverseMassRes
    );
    MassProperties::CalculateRectangularCuboid(size, 1.0f, inverseInertiaCmp, inverseMassCmp);
    TEST_ASSERT(AlmostEqual(inverseMassRes, inverseMassCmp));
    TEST_ASSERT(AlmostEqual(inverseInertiaRes, inverseInertiaCmp));
    TEST_ASSERT(AlmostEqual(centerOfMass, Vec3{0.0f}));

    size = {10.0f, 4.0f, 0.5f};
    MassProperties::CalculatePolyhedronTriangleMesh(
        positions.mData,
        indices.mData,
        indices.mCount,
        density,
        size,
        inverseInertiaRes,
        centerOfMass,
        inverseMassRes
    );
    MassProperties::CalculateRectangularCuboid(size, density, inverseInertiaCmp, inverseMassCmp);
    TEST_ASSERT(AlmostEqual(inverseMassRes, inverseMassCmp));
    TEST_ASSERT(AlmostEqual(inverseInertiaRes, inverseInertiaCmp));
    TEST_ASSERT(AlmostEqual(centerOfMass, Vec3{0.0f}));
}

#endif
