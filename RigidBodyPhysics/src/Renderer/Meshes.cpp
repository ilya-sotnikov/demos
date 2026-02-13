#include "Meshes.hpp"

#include "../Math/Vec3.hpp"

struct Vertex
{
    Vec3 mPosition;
    Vec3 mNormal;
};

void GetCubeData(Slice<Vec3>& positions, Slice<u16>& indices, Slice<Vec3>* normals, Arena& arena)
{
    // clang-format off
    const Vertex verticesArray[] = {
        {{-0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}},

        {{ 0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}},
        {{-0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}},

        {{-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}},

        {{ 0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}},
        {{-0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}},
        {{-0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}},

        {{ 0.5f,  0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}},

        {{-0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}},
        {{-0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}},

        {{-0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}},

        {{-0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}},

        {{-0.5f,  0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}},

        {{ 0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}},

        {{ 0.5f,  0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}},

        {{ 0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}},
    };
    // clang-format on

    // clang-format off
    const u16 indicesArray[] = {
        0, 1, 2,
        3, 4, 5,
        6, 7, 8,
        9, 10, 11,
        12, 13, 14,
        15, 16, 17,
        0, 18, 1,
        3, 19, 4,
        6, 20, 7,
        9, 21, 10,
        12, 22, 13,
        15, 23, 16,
    };
    // clang-format on

    const int verticesCount = ARRAY_SIZE(verticesArray);
    const int indicesCount = ARRAY_SIZE(indicesArray);

    positions.mCount = verticesCount;
    positions.mData = arena.AllocOrDie<Vec3>(positions.mCount);

    for (int i = 0; i < verticesCount; ++i)
    {
        positions.mData[i] = verticesArray[i].mPosition;
    }

    indices.mCount = indicesCount;
    indices.mData = arena.AllocOrDie<u16>(indices.mCount);
    for (int i = 0; i < indicesCount; ++i)
    {
        indices.mData[i] = indicesArray[i];
    }

    if (normals)
    {
        normals->mCount = verticesCount;
        normals->mData = arena.AllocOrDie<Vec3>(normals->mCount);
        for (int i = 0; i < verticesCount; ++i)
        {
            normals->mData[i] = verticesArray[i].mNormal;
        }
    }
}

void GetSphereData(Slice<Vec3>& positions, Slice<u16>& indices, Slice<Vec3>* normals, Arena& arena)
{
    constexpr u16 sectorCount = 32;
    constexpr u16 stackCount = 32;
    constexpr f32 radius = 1.0f;
    constexpr f32 inverseLength = 1.0f / radius;

    positions.mCount = (sectorCount + 1) * (stackCount + 1);
    positions.mData = arena.AllocOrDie<Vec3>(positions.mCount);

    // Basically a spherical coordinate system:
    // https://www.songho.ca/opengl/gl_sphere.html
    int count = 0;
    for (uint i = 0; i <= stackCount; ++i)
    {
        const f32 sectorStep = 2.0f * M_PIf / sectorCount;
        const f32 stackStep = M_PIf / stackCount;

        const f32 stackAngle = M_PI_2f - static_cast<f32>(i) * stackStep; // [-Pi/2, Pi/2]
        const f32 xz = radius * cosf(stackAngle);
        const f32 y = radius * sinf(stackAngle);

        for (uint j = 0; j <= sectorCount; ++j)
        {
            const f32 sectorAngle = static_cast<f32>(j) * sectorStep; // [0, 2*Pi]
            const f32 x = xz * cosf(sectorAngle);
            const f32 z = xz * sinf(sectorAngle);
            positions.mData[count++] = {x, y, z};
        }
    }
    assert(count == positions.mCount);

    indices.mCount = (sectorCount - 1) * stackCount * 6;
    indices.mData = arena.AllocOrDie<u16>(indices.mCount);

    // Generate CCW index list of sphere triangles.
    count = 0;
    for (u16 i = 0; i < stackCount; ++i)
    {
        u16 k1 = i * (sectorCount + 1U); // Beginning of current stack.
        u16 k2 = k1 + sectorCount + 1U; // Beginning of next stack.

        for (u16 j = 0; j < sectorCount; ++j)
        {
            if (i != 0)
            {
                indices.mData[count++] = k1;
                indices.mData[count++] = k1 + 1;
                indices.mData[count++] = k2;
            }

            if (i != (stackCount - 1))
            {
                indices.mData[count++] = k1 + 1;
                indices.mData[count++] = k2 + 1;
                indices.mData[count++] = k2;
            }

            ++k1;
            ++k2;
        }
    }
    assert(count == indices.mCount);

    if (normals)
    {
        normals->mCount = positions.mCount;
        normals->mData = arena.AllocOrDie<Vec3>(normals->mCount);
        for (int i = 0; i < positions.mCount; ++i)
        {
            const Vec3 p = positions.mData[i];
            normals->mData[i]
                = {p.X() * inverseLength, p.Y() * inverseLength, p.Z() * inverseLength};
        }
    }
}

// Regular tetrahedron with side == 1.
void GetTetrahedronData(
    Slice<Vec3>& positions,
    Slice<u16>& indices,
    Slice<Vec3>* normals,
    Arena& arena
)
{
    // clang-format off
    const Vec3 n1 = { 0.0000f,  0.3333f, -0.9428f};
    const Vec3 n2 = {-0.8165f,  0.3333f,  0.4714f};
    const Vec3 n3 = { 0.8165f,  0.3333f,  0.4714f};
    const Vec3 n4 = { 0.0000f, -1.0000f,  0.0000f};
    const Vertex verticesArray[] = {
        {{ 0.000000f,  0.614170f,  0.000000f}, {n1}},
        {{ 0.501468f, -0.204723f, -0.289523f}, {n1}},
        {{-0.501468f, -0.204723f, -0.289523f}, {n1}},

        {{ 0.000000f,  0.614170f,  0.000000f}, {n2}},
        {{-0.501468f, -0.204723f, -0.289523f}, {n2}},
        {{ 0.000000f, -0.204723f,  0.579045f}, {n2}},

        {{0.000000f,   0.614170f,  0.000000f}, {n3}},
        {{0.000000f,  -0.204723f,  0.579045f}, {n3}},
        {{0.501468f,  -0.204723f, -0.289523f}, {n3}},

        {{ 0.501468f, -0.204723f, -0.289523f}, {n4}},
        {{ 0.000000f, -0.204723f,  0.579045f}, {n4}},
        {{-0.501468f, -0.204723f, -0.289523f}, {n4}},
    };
    // clang-format on

    // clang-format off
    const u16 indicesArray[] = {
        0, 1, 2,
        3, 4, 5,
        6, 7, 8,
        9, 10, 11,
    };
    // clang-format on

    const int verticesCount = ARRAY_SIZE(verticesArray);
    const int indicesCount = ARRAY_SIZE(indicesArray);

    positions.mCount = verticesCount;
    positions.mData = arena.AllocOrDie<Vec3>(positions.mCount);

    for (int i = 0; i < verticesCount; ++i)
    {
        positions.mData[i] = verticesArray[i].mPosition;
    }

    indices.mCount = indicesCount;
    indices.mData = arena.AllocOrDie<u16>(indices.mCount);
    for (int i = 0; i < indicesCount; ++i)
    {
        indices.mData[i] = indicesArray[i];
    }

    if (normals)
    {
        normals->mCount = positions.mCount;
        normals->mData = arena.AllocOrDie<Vec3>(normals->mCount);
        for (int i = 0; i < verticesCount; ++i)
        {
            normals->mData[i] = verticesArray[i].mNormal;
        }
    }
}
