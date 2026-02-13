#pragma once

#include "../Common.hpp"

#include "Config.hpp"
#include "../Math/Types.hpp"

struct TransformMat
{
    Mat3 mRotation;
    Vec3 mTranslation;
};

struct TransformQuat
{
    Quat mRotation;
    Vec3 mTranslation;
};

struct Plane
{
    Vec3 mNormal;
    f32 mOffset;
};

struct FeatureId
{
    static constexpr u8 EDGE_NULL = UINT8_MAX;

    // R -- reference, I -- incident
    u8 mInHalfEdgeR;
    u8 mOutHalfEdgeR;
    u8 mInHalfEdgeI;
    u8 mOutHalfEdgeI;

    void Flip();
};

struct ClipVertex
{
    Vec3 mPosition;
    FeatureId mFeatureId;
};

// Half-edge (DCEL) data structure.
struct ConvexHull
{
    using Id = int;

    static constexpr u8 PRIMITIVE_NULL = FeatureId::EDGE_NULL;
    static constexpr u8 PRIMITIVE_MAX = UINT8_MAX;

    struct HalfEdge
    {
        u8 mNext;
        u8 mTwin;
        u8 mOrigin;
        u8 mFace;

        static HalfEdge Null();
    };

    struct Vertex
    {
        u8 mHalfEdge;
    };

    struct Face
    {
        u8 mHalfEdge;
    };

    Vec3 mCentroid;
    Vec3 mScale;
    Vertex* mVertices; // TODO: unused so far.
    Vec3* mVertexPositions;
    int mVerticesCount;
    HalfEdge* mHalfEdges;
    int mHalfEdgesCount;
    Face* mFaces;
    Plane* mFacePlanes;
    int mFacesCount;
    f32 mRadius;

    Slice<Vec3> mMeshPositions;
    Slice<u16> mMeshIndices;
    int mMeshIndicesCount;

    // TODO: after implementing quickhull these specialized functions will become unnecessary.
    void InitBox(Vec3 scale);
    void InitTetrahedron(Vec3 scale);

    Vec3 GetSupportPoint(Vec3 direction) const;
    HalfEdge GetNext(u8 halfEdgeIndex) const;
    Vec3 GetOrigin(u8 halfEdgeIndex) const;
    Vec3 GetTarget(u8 halfEdgeIndex) const;
    int GetVertices(Vec3 vertices[PRIMITIVE_MAX], u8 faceIndex) const;
    int GetClipVertices(ClipVertex vertices[PRIMITIVE_MAX], u8 faceIndex) const;
    int GetSidePlanes(
        Plane planes[PRIMITIVE_MAX],
        u8 planesHalfEdgeIndices[PRIMITIVE_MAX],
        u8 faceIndex
    ) const;

    void DebugDraw() const;

    enum class ConsistencyResult
    {
        Ok,
        EulerCharacteristicIsNot2,
        HalfEdgesCountOdd,
        HalfEdgeNull,
        HalfEdgeIndexOutOfBounds,
        HalfEdgeSomethingNull,
        HalfEdgeWrongFace,
        HalfEdgeWrongTwin,
        FaceWrongNormal,
    };

    ConsistencyResult CheckConsistency() const;
};

int GetSupportPointIndex(const Vec3* vertices, int verticesCount, Vec3 direction);
Vec3 GetSupportPoint(const Vec3* vertices, int verticesCount, Vec3 direction);
int GetSupportPointIndex(const ClipVertex* vertices, int verticesCount, Vec3 direction);
ClipVertex GetSupportPoint(const ClipVertex* vertices, int verticesCount, Vec3 direction);

Plane Transform(const TransformMat& transform, Plane plane);
Vec3 Transform(const TransformMat& transform, Vec3 v);
Vec3 InverseTransform(const TransformMat& transform, Vec3 v);

Vec3 ClosestPoint(Plane plane, Vec3 point);
f32 Distance(Plane plane, Vec3 point);
f32 Project(Plane plane, const ConvexHull& hull);
