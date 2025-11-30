#include "Geometry.hpp"

#include "../Arena.hpp"
#include "Config.hpp"
#include "../Math/Utils.hpp"
#include "../Math/Vec3.hpp"
#include "../Math/Mat3.hpp"
#include "../Math/Quat.hpp"
#include "../Renderer/Renderer.hpp"
#include "../Renderer/Meshes.hpp"

#include <float.h>
#include <stdio.h>

void FeatureId::Flip()
{
    Swap(mInHalfEdgeR, mInHalfEdgeI);
    Swap(mOutHalfEdgeR, mOutHalfEdgeI);
}

int GetSupportPointIndex(const Vec3* vertices, int verticesCount, Vec3 direction)
{
    assert(vertices);
    assert(verticesCount > 0);

    int maxIndex = -1;
    f32 maxProjection = -FLT_MAX;

    for (int i = 0; i < verticesCount; ++i)
    {
        const f32 projection = Dot(direction, vertices[i]);
        if (projection > maxProjection)
        {
            maxIndex = i;
            maxProjection = projection;
        }
    }

    assert(maxIndex > -1);

    return maxIndex;
}

Vec3 GetSupportPoint(const Vec3* vertices, int verticesCount, Vec3 direction)
{
    assert(vertices);
    assert(verticesCount > 0);

    return vertices[GetSupportPointIndex(vertices, verticesCount, direction)];
}

int GetSupportPointIndex(const ClipVertex* vertices, int verticesCount, Vec3 direction)
{
    assert(vertices);
    assert(verticesCount > 0);

    int maxIndex = -1;
    f32 maxProjection = -FLT_MAX;

    for (int i = 0; i < verticesCount; ++i)
    {
        const f32 projection = Dot(direction, vertices[i].mPosition);
        if (projection > maxProjection)
        {
            maxIndex = i;
            maxProjection = projection;
        }
    }

    assert(maxIndex > -1);

    return maxIndex;
}

ClipVertex GetSupportPoint(const ClipVertex* vertices, int verticesCount, Vec3 direction)
{
    assert(vertices);
    assert(verticesCount > 0);

    return vertices[GetSupportPointIndex(vertices, verticesCount, direction)];
}

Plane Transform(const TransformMat& transform, Plane plane)
{
    const Vec3 normal = transform.mRotation * plane.mNormal;
    return Plane{normal, plane.mOffset + Dot(normal, transform.mTranslation)};
}

Vec3 Transform(const TransformMat& transform, Vec3 v)
{
    return transform.mRotation * v + transform.mTranslation;
}

Vec3 InverseTransform(const TransformMat& transform, Vec3 v)
{
    return TMul(transform.mRotation, v - transform.mTranslation);
}

ConvexHull::HalfEdge Null()
{
    return {
        ConvexHull::PRIMITIVE_NULL,
        ConvexHull::PRIMITIVE_NULL,
        ConvexHull::PRIMITIVE_NULL,
        ConvexHull::PRIMITIVE_NULL
    };
}

/*
 * Half-edges:
 * < left     > right     ^ away     v towards
 *
 * Vertices:
 * A number in parentheses.
 *
 * Faces:
 * A number, on the right box.
 *
 * TODO: ahem, quickhull.
 *
 *             (7)     >12
 *             ,+--------------------+(6)            ,+--------------------+
 *           ,' |      <13         ,'|             ,' |        5 (back)  ,'|
 *      ^14,'   |            ^11 ,'  |           ,'   |     2          ,'  |
 *       ,'v15  |   >9         ,'v10 |         ,'     |              ,'    |
 *   (4)+-------+------------+'      |        +-------+------------+'      |
 *      |       |   <8       |(5)    |        |       |            |       |
 *      |       |            |    v20|^21     |       |            |       |
 *      |    v22|^23         |       |        |       |            |       |
 *      |       |         v19|^18    |        |  3    |            |   1   |
 *      |       |            |       |        |       | 4 (front)  |       |
 *   v16|^17    |(3)   >4    |       |        |       |            |       |
 *      |      ,+------------+-------+(2)     |      ,+------------+-------+
 *      |  ^6,'        <5    |     ,'         |    ,'              |     ,'
 *      |  ,' v7             | ^3,'           |  ,'        0       |   ,'
 *      |,'       >1         | ,' v2          |,'                  | ,'
 *      +--------------------+'               +--------------------+'
 *    (0)         <0         (1)
 *
 */
void ConvexHull::InitBox(Vec3 scale)
{
    assert(scale.X() > 0.0f);
    assert(scale.Y() > 0.0f);
    assert(scale.Z() > 0.0f);

    mVerticesCount = 8;
    mHalfEdgesCount = 12 * 2; // 12 edges.
    mFacesCount = 6;

    mScale = scale;

    mVertexPositions = gArenaReset.AllocOrDie<Vec3>(mVerticesCount);
    // clang-format off
    // Bottom face.
    mVertexPositions[0] = {-0.5f, -0.5f,  0.5f};
    mVertexPositions[1] = { 0.5f, -0.5f,  0.5f};
    mVertexPositions[2] = { 0.5f, -0.5f, -0.5f};
    mVertexPositions[3] = {-0.5f, -0.5f, -0.5f};
    // Top face.
    mVertexPositions[4] = {-0.5f,  0.5f,  0.5f};
    mVertexPositions[5] = { 0.5f,  0.5f,  0.5f};
    mVertexPositions[6] = { 0.5f,  0.5f, -0.5f};
    mVertexPositions[7] = {-0.5f,  0.5f, -0.5f};
    // clang-format on
    mCentroid = {0.0f, 0.0f, 0.0f};

    mHalfEdges = gArenaReset.AllocOrDie<HalfEdge>(mHalfEdgesCount);
    // clang-format off
    mHalfEdges[0]  = {17, 1,  1, 4};
    mHalfEdges[1]  = {3,  0,  0, 0};
    mHalfEdges[2]  = {18, 3,  2, 1};
    mHalfEdges[3]  = {5,  2,  1, 0};
    mHalfEdges[4]  = {21, 5,  3, 5};
    mHalfEdges[5]  = {7,  4,  2, 0};
    mHalfEdges[6]  = {23, 7,  0, 3};
    mHalfEdges[7]  = {1,  6,  3, 0};
    mHalfEdges[8]  = {14, 9,  5, 2};
    mHalfEdges[9]  = {19, 8,  4, 4};
    mHalfEdges[10] = {8,  11, 6, 2};
    mHalfEdges[11] = {20, 10, 5, 1};
    mHalfEdges[12] = {10, 13, 7, 2};
    mHalfEdges[13] = {22, 12, 6, 5};
    mHalfEdges[14] = {12, 15, 4, 2};
    mHalfEdges[15] = {16, 14, 7, 3};
    mHalfEdges[16] = {6,  17, 4, 3};
    mHalfEdges[17] = {9,  16, 0, 4};
    mHalfEdges[18] = {11, 19, 1, 1};
    mHalfEdges[19] = {0,  18, 5, 4};
    mHalfEdges[20] = {2,  21, 6, 1};
    mHalfEdges[21] = {13, 20, 2, 5};
    mHalfEdges[22] = {4,  23, 7, 5};
    mHalfEdges[23] = {15, 22, 3, 3};
    // clang-format on

    mFaces = gArenaReset.AllocOrDie<Face>(mFacesCount);
    mFaces[0].mHalfEdge = 1;
    mFaces[1].mHalfEdge = 2;
    mFaces[2].mHalfEdge = 10;
    mFaces[3].mHalfEdge = 6;
    mFaces[4].mHalfEdge = 0;
    mFaces[5].mHalfEdge = 4;

    mFacePlanes = gArenaReset.AllocOrDie<Plane>(mFacesCount);
    // clang-format off
    mFacePlanes[0] = {{ 0.0f, -1.0f,  0.0f}, -0.5f};
    mFacePlanes[1] = {{ 1.0f,  0.0f,  0.0f},  0.5f};
    mFacePlanes[2] = {{ 0.0f,  1.0f,  0.0f},  0.5f};
    mFacePlanes[3] = {{-1.0f,  0.0f,  0.0f}, -0.5f};
    mFacePlanes[4] = {{ 0.0f,  0.0f,  1.0f},  0.5f};
    mFacePlanes[5] = {{ 0.0f,  0.0f, -1.0f}, -0.5f};
    // clang-format on

    for (int i = 0; i < mVerticesCount; ++i)
    {
        mVertexPositions[i] *= scale;
    }

    mRadius = Magnitude(mVertexPositions[3] - mVertexPositions[5]) / 2.0f;

    for (int i = 0; i < mFacesCount; ++i)
    {
        mFacePlanes[i].mOffset *= Dot(mFacePlanes[i].mNormal, scale);
    }

    const ConsistencyResult consistency = CheckConsistency();
    if (consistency != ConsistencyResult::Ok)
    {
        fprintf(stderr, "Box consistency error: %d\n", static_cast<int>(consistency));
    }
    assert(consistency == ConsistencyResult::Ok);

    GetCubeData(mMeshPositions, mMeshIndices, nullptr, gArenaReset);
}

/* For the explanation, refer to the cube above.
 * TODO: I really need to implement quickhull.
 *
 *                                                 ^ Y
 *               (3)                               |
 *                _                                |
 *               /|\                              /|\
 *              / | \                            / | \
 *             /  |  \                          /  |  \
 *            /   |   \                        /   |   \
 *           /    |    \                      /3 (front)\
 *      ^10 /v11  |   ^7\ v6                 /     |     \
 *         /      |      \                  /      |      \
 *        /     v9|^8     \                /       ^ Z     \
 *       /        |        \              /  2     |     1  \
 *      /      ,-' `-. v2   \            / <-------*`-.      \
 *     /  ^4,-'  (2)  `-.    \          /  X ,-'       `-.    \
 *    /  ,-'v5        ^3 `-.  \        /  ,-'  0 (bot)    `-.  \
 *   /,-'        >1         `-.\      /,-'                   `-.\
 *  |---------------------------|    |---------------------------|
 * (0)           <0            (1)
 *
 *
 */
// Regular tetrahedron with side == 1.
void ConvexHull::InitTetrahedron(Vec3 scale)
{
    assert(scale.X() > 0.0f);
    assert(scale.Y() > 0.0f);
    assert(scale.Z() > 0.0f);

    mVerticesCount = 4;
    mHalfEdgesCount = 6 * 2; // 6 edges.
    mFacesCount = 4;

    mScale = scale;

    mVertexPositions = gArenaReset.AllocOrDie<Vec3>(mVerticesCount);
    // clang-format off
    mVertexPositions[0] = { 0.501468f, -0.204723f, -0.289523f};
    mVertexPositions[1] = {-0.501468f, -0.204723f, -0.289523f};
    mVertexPositions[2] = { 0.000000f, -0.204723f,  0.579045f};
    mVertexPositions[3] = { 0.000000f,  0.614170f,  0.000000f};
    // clang-format on
    mCentroid = Vec3{0.0f};
    mRadius = Max(scale.X(), scale.Y(), scale.Z());

    mHalfEdges = gArenaReset.AllocOrDie<HalfEdge>(mHalfEdgesCount);
    // clang-format off
    mHalfEdges[0]  = {10, 1,  1, 3};
    mHalfEdges[1]  = {3,  0,  0, 0};
    mHalfEdges[2]  = {7,  3,  2, 1};
    mHalfEdges[3]  = {5,  2,  1, 0};
    mHalfEdges[4]  = {8,  5,  0, 2};
    mHalfEdges[5]  = {1,  4,  2, 0};
    mHalfEdges[6]  = {0,  7,  3, 3};
    mHalfEdges[7]  = {9,  6,  1, 1};
    mHalfEdges[8]  = {11, 9,  2, 2};
    mHalfEdges[9]  = {2,  8,  3, 1};
    mHalfEdges[10] = {6,  11, 0, 3};
    mHalfEdges[11] = {4,  10, 3, 2};
    // clang-format on

    mFaces = gArenaReset.AllocOrDie<Face>(mFacesCount);
    mFaces[0].mHalfEdge = 1;
    mFaces[1].mHalfEdge = 2;
    mFaces[2].mHalfEdge = 4;
    mFaces[3].mHalfEdge = 0;

    mFacePlanes = gArenaReset.AllocOrDie<Plane>(mFacesCount);
    // clang-format off
    mFacePlanes[0] = {{ 0.0000f, -1.0000f,  0.0000f}, 0.0f};
    mFacePlanes[1] = {{-0.8165f,  0.3333f,  0.4714f}, 0.0f};
    mFacePlanes[2] = {{ 0.8165f,  0.3333f,  0.4714f}, 0.0f};
    mFacePlanes[3] = {{ 0.0000f,  0.3333f, -0.9428f}, 0.0f};
    // clang-format on

    for (int i = 0; i < mFacesCount; ++i)
    {
        mVertexPositions[i] *= scale;
    }

    // For non-uniform scaling.
    const Vec3 inverseScale = {1.0f / scale.X(), 1.0f / scale.Y(), 1.0f / scale.Z()};
    for (int i = 0; i < mFacesCount; ++i)
    {
        mFacePlanes[i].mNormal = Normalize(mFacePlanes[i].mNormal * inverseScale);
        const Vec3 pointOnFace = mVertexPositions[mHalfEdges[mFaces[i].mHalfEdge].mOrigin];
        mFacePlanes[i].mOffset = Dot(mFacePlanes[i].mNormal, pointOnFace);
    }

    const ConsistencyResult consistency = CheckConsistency();
    if (consistency != ConsistencyResult::Ok)
    {
        fprintf(stderr, "Tetrahedron consistency error: %d\n", static_cast<int>(consistency));
    }
    assert(consistency == ConsistencyResult::Ok);

    GetTetrahedronData(mMeshPositions, mMeshIndices, nullptr, gArenaReset);
}

Vec3 ConvexHull::GetSupportPoint(Vec3 direction) const
{
    const int index = GetSupportPointIndex(mVertexPositions, mVerticesCount, direction);
    assert(index >= 0);
    return mVertexPositions[index];
}

ConvexHull::HalfEdge ConvexHull::GetNext(u8 halfEdgeIndex) const
{
    return mHalfEdges[mHalfEdges[halfEdgeIndex].mNext];
}

Vec3 ConvexHull::GetOrigin(u8 halfEdgeIndex) const
{
    return mVertexPositions[mHalfEdges[halfEdgeIndex].mOrigin];
}

Vec3 ConvexHull::GetTarget(u8 halfEdgeIndex) const
{
    return mVertexPositions[GetNext(halfEdgeIndex).mOrigin];
}

int ConvexHull::GetVertices(Vec3 vertices[PRIMITIVE_MAX], u8 faceIndex) const
{
    assert(vertices);

    int vertexCount = 0;

    const u8 halfEdgeIndexBegin = mFaces[faceIndex].mHalfEdge;
    u8 halfEdgeIndex = halfEdgeIndexBegin;
    do
    {
        const HalfEdge halfEdge = mHalfEdges[halfEdgeIndex];
        vertices[vertexCount++] = mVertexPositions[halfEdge.mOrigin];
        halfEdgeIndex = halfEdge.mNext;
    }
    while (halfEdgeIndex != halfEdgeIndexBegin);

    return vertexCount;
}

int ConvexHull::GetClipVertices(ClipVertex vertices[PRIMITIVE_MAX], u8 faceIndex) const
{
    assert(vertices);

    int vertexCount = 0;

    const u8 halfEdgeIndexBegin = mFaces[faceIndex].mHalfEdge;
    u8 halfEdgeIndex = halfEdgeIndexBegin;
    u8 prevHalfEdgeIndex = halfEdgeIndex;
    do
    {
        const HalfEdge halfEdge = mHalfEdges[halfEdgeIndex];

        vertices[vertexCount].mPosition = mVertexPositions[halfEdge.mOrigin];
        vertices[vertexCount].mFeatureId.mInHalfEdgeI = prevHalfEdgeIndex;
        vertices[vertexCount].mFeatureId.mOutHalfEdgeI = halfEdgeIndex;
        vertices[vertexCount].mFeatureId.mInHalfEdgeR = PRIMITIVE_NULL;
        vertices[vertexCount].mFeatureId.mOutHalfEdgeR = PRIMITIVE_NULL;

        ++vertexCount;

        prevHalfEdgeIndex = halfEdgeIndex;
        halfEdgeIndex = halfEdge.mNext;
    }
    while (halfEdgeIndex != halfEdgeIndexBegin);

    vertices[0].mFeatureId.mInHalfEdgeI = prevHalfEdgeIndex;

    return vertexCount;
}

int ConvexHull::GetSidePlanes(
    Plane planes[PRIMITIVE_MAX],
    u8 planesHalfEdgeIndices[PRIMITIVE_MAX],
    u8 faceIndex
) const
{
    assert(planes);
    assert(planesHalfEdgeIndices);

    int planesCount = 0;

    const u8 halfEdgeIndexBegin = mFaces[faceIndex].mHalfEdge;
    u8 halfEdgeIndex = halfEdgeIndexBegin;
    do
    {
        const HalfEdge halfEdge = mHalfEdges[halfEdgeIndex];
        const HalfEdge twinHalfEdge = mHalfEdges[halfEdge.mTwin];
        planesHalfEdgeIndices[planesCount] = halfEdge.mTwin;
        planes[planesCount++] = mFacePlanes[twinHalfEdge.mFace];
        halfEdgeIndex = halfEdge.mNext;
    }
    while (halfEdgeIndex != halfEdgeIndexBegin);

    return planesCount;
}

static bool IsConsistent(ConvexHull::HalfEdge halfEdge)
{
    return halfEdge.mNext != ConvexHull::PRIMITIVE_NULL
        && halfEdge.mTwin != ConvexHull::PRIMITIVE_NULL
        && halfEdge.mOrigin != ConvexHull::PRIMITIVE_NULL
        && halfEdge.mFace != ConvexHull::PRIMITIVE_NULL;
}

ConvexHull::ConsistencyResult ConvexHull::CheckConsistency() const
{
    // TODO: more checks.

    if (mHalfEdgesCount & 1)
    {
        return ConsistencyResult::HalfEdgesCountOdd;
    }

    // Any 3d convex polyhedra's surface has an Euler characteristic of 2.
    if (mVerticesCount - mHalfEdgesCount / 2 + mFacesCount != 2)
    {
        return ConsistencyResult::EulerCharacteristicIsNot2;
    }

    for (int i = 0; i < mFacesCount; ++i)
    {
        u8 firstHalfEdgeIndex = mFaces[i].mHalfEdge;
        if (firstHalfEdgeIndex == PRIMITIVE_NULL)
        {
            return ConsistencyResult::HalfEdgeNull;
        }
        if (firstHalfEdgeIndex >= mHalfEdgesCount)
        {
            return ConsistencyResult::HalfEdgeIndexOutOfBounds;
        }
        if (!IsConsistent(mHalfEdges[firstHalfEdgeIndex]))
        {
            return ConsistencyResult::HalfEdgeSomethingNull;
        }
        u8 halfEdgeIndex = firstHalfEdgeIndex;
        do
        {
            if (halfEdgeIndex >= mHalfEdgesCount)
            {
                return ConsistencyResult::HalfEdgeIndexOutOfBounds;
            }

            const HalfEdge halfEdge = mHalfEdges[halfEdgeIndex];

            if (!IsConsistent(halfEdge))
            {
                return ConsistencyResult::HalfEdgeSomethingNull;
            }

            if (halfEdge.mFace != i)
            {
                return ConsistencyResult::HalfEdgeWrongFace;
            }

            const Vec3 edge = GetTarget(halfEdgeIndex) - GetOrigin(halfEdgeIndex);
            if (!AlmostEqual(Dot(edge, mFacePlanes[i].mNormal), 0.0f, 0.001f))
            {
                return ConsistencyResult::FaceWrongNormal;
            }

            halfEdgeIndex = mHalfEdges[halfEdgeIndex].mNext;
        }
        while (halfEdgeIndex != firstHalfEdgeIndex);
    }

    // So we can skip every other half-edge in edge x edge SAT testing.
    for (int i = 0; i < mHalfEdgesCount; i += 2)
    {
        const HalfEdge halfEdge = mHalfEdges[i];
        const HalfEdge twin = mHalfEdges[halfEdge.mTwin];

        if (halfEdge.mTwin != i + 1)
        {
            return ConsistencyResult::HalfEdgeWrongTwin;
        }

        if (twin.mTwin != i)
        {
            return ConsistencyResult::HalfEdgeWrongTwin;
        }
    }

    return ConsistencyResult::Ok;
}

void ConvexHull::DebugDraw() const
{
    for (int i = 0; i < mFacesCount; ++i)
    {
        const ConvexHull::Face face = mFaces[i];
        const u8 firstEdgeIdx = face.mHalfEdge;
        u8 edgeIndex = face.mHalfEdge;
        do
        {
            const ConvexHull::HalfEdge edge = mHalfEdges[edgeIndex];
            const ConvexHull::HalfEdge edgeNext = mHalfEdges[edge.mNext];
            gRenderer.DrawLine(
                mVertexPositions[edge.mOrigin],
                mVertexPositions[edgeNext.mOrigin],
                gColorSequence[i % gColorSequenceCount]
            );
            edgeIndex = mHalfEdges[edgeIndex].mNext;
        }
        while (edgeIndex != firstEdgeIdx);
    }
    for (int i = 0; i < mFacesCount; ++i)
    {
        const Plane plane = mFacePlanes[i];
        gRenderer.DrawLineOrigin(plane.mNormal * plane.mOffset, plane.mNormal, {0, 255, 0});
    }
    gRenderer.DrawPoint(mCentroid, 0.05f, {255, 0, 0});
}

Vec3 ClosestPoint(Plane plane, Vec3 point)
{
    const f32 offset = Dot(plane.mNormal, point) - plane.mOffset;
    return point - offset * plane.mNormal;
}

f32 Distance(Plane plane, Vec3 point)
{
    return Dot(plane.mNormal, point) - plane.mOffset;
}

f32 Project(Plane plane, const ConvexHull& hull)
{
    const Vec3 support = hull.GetSupportPoint(-plane.mNormal);
    return Distance(plane, support);
}
