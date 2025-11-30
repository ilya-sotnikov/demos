#include "Collide.hpp"

#include "Config.hpp"
#include "Geometry.hpp"
#include "GJK.hpp"
#include "../Renderer/Renderer.hpp"
#include "../Math/Utils.hpp"
#include "../Math/Vec3.hpp"
#include "../Math/Mat3.hpp"
#include "../Math/Quat.hpp"

#include <string.h>

struct HullFaceQuery
{
    int mFaceIndex;
    f32 mSeparation;
};

struct HullEdgeQuery
{
    int mEdgeIndex1;
    int mEdgeIndex2;
    f32 mSeparation;
};

static int ReduceContactPoints(
    ClipVertex out[4],
    Vec3* positions,
    FeatureId* featureIds,
    f32* separations,
    int pointsCount,
    Vec3 pointsNormal
)
{
    assert(out);
    assert(positions);
    assert(featureIds);
    assert(separations);
    assert(pointsCount >= ContactManifold::CONTACT_MAX_POINTS);

    f32 tmp = FLT_MAX;
    int index = -1;
    for (int i = 0; i < pointsCount; ++i)
    {
        if (separations[i] < tmp)
        {
            tmp = separations[i];
            index = i;
        }
    }
    assert(index > -1);

    const Vec3 point1 = positions[index];
    int reducedCount = 0;
    out[reducedCount++] = {positions[index], featureIds[index]};

    tmp = -FLT_MAX;
    index = -1;
    for (int i = 0; i < pointsCount; ++i)
    {
        const f32 distanceSq = MagnitudeSq(point1 - positions[i]);
        if (distanceSq > tmp)
        {
            tmp = distanceSq;
            index = i;
        }
    }
    assert(index > -1);

    const Vec3 point2 = positions[index];
    out[reducedCount++] = {positions[index], featureIds[index]};

    index = -1;
    tmp = -FLT_MAX;
    for (int i = 0; i < pointsCount; ++i)
    {
        const Vec3 point3 = positions[i];
        const f32 signedArea = 0.5f * Dot(Cross(point1 - point3, point2 - point3), pointsNormal);
        if (signedArea > tmp)
        {
            tmp = signedArea;
            index = i;
        }
    }
    assert(index > -1);

    const Vec3 point3 = positions[index];
    out[reducedCount++] = {positions[index], featureIds[index]};

    index = -1;
    tmp = 0.0f; // Finding a minimum of negative values.
    for (int i = 0; i < pointsCount; ++i)
    {
        const Vec3 point4 = positions[i];
        const f32 signedArea1 = 0.5f * Dot(Cross(point1 - point4, point2 - point4), pointsNormal);
        if (signedArea1 < tmp)
        {
            tmp = signedArea1;
            index = i;
        }
        const f32 signedArea2 = 0.5f * Dot(Cross(point2 - point4, point3 - point4), pointsNormal);
        if (signedArea2 < tmp)
        {
            tmp = signedArea2;
            index = i;
        }
        const f32 signedArea3 = 0.5f * Dot(Cross(point3 - point4, point1 - point4), pointsNormal);
        if (signedArea3 < tmp)
        {
            tmp = signedArea3;
            index = i;
        }
    }
    assert(index > -1);

    out[reducedCount++] = {positions[index], featureIds[index]};

    return reducedCount;
}

static HullFaceQuery HullQueryFaceDirections(
    const TransformMat& transform1,
    const TransformMat& transform2,
    const ConvexHull& hull1,
    const ConvexHull& hull2
)
{
    // Transform from local space of first to second.
    const TransformMat transform{
        TMul(transform2.mRotation, transform1.mRotation),
        TMul(transform2.mRotation, transform1.mTranslation - transform2.mTranslation)
    };

    int maxIndex = -1;
    f32 maxSeparation = -FLT_MAX;

    const int faceCount1 = hull1.mFacesCount;

    for (int i = 0; i < faceCount1; ++i)
    {
        const Plane plane = Transform(transform, hull1.mFacePlanes[i]);
        const f32 separation = Project(plane, hull2);
        if (separation > maxSeparation)
        {
            maxIndex = i;
            maxSeparation = separation;
        }
    }
    assert(maxIndex > -1);

    return {maxIndex, maxSeparation};
}

static bool IsMinkowskiFace(Vec3 a, Vec3 b, Vec3 crossBA, Vec3 c, Vec3 d, Vec3 crossDC)
{
    const f32 cba = Dot(c, crossBA);
    const f32 dba = Dot(d, crossBA);
    const f32 adc = Dot(a, crossDC);
    const f32 bdc = Dot(b, crossDC);

    return cba * dba < 0.0f && adc * bdc < 0.0f && cba * bdc > 0.0f;
}

static f32 Project(Vec3 p1, Vec3 e1, Vec3 p2, Vec3 e2, Vec3 c1)
{
    constexpr f32 TOLERANCE = 0.005f;

    const Vec3 crossE1E2 = Cross(e1, e2);

    const f32 l = Magnitude(crossE1E2);
    if (l < TOLERANCE * sqrtf(MagnitudeSq(e1) * MagnitudeSq(e2)))
    {
        return -FLT_MAX;
    }

    Vec3 n = crossE1E2 / l;
    if (Dot(n, p1 - c1) < 0.0f)
    {
        n = -n;
    }

    return Dot(n, p2 - p1);
}

static HullEdgeQuery HullQueryEdgeDirections(
    const TransformMat& transform1,
    const TransformMat& transform2,
    const ConvexHull& hull1,
    const ConvexHull& hull2
)
{
    // Transform from local space of first to second.
    const TransformMat transform{
        TMul(transform2.mRotation, transform1.mRotation),
        TMul(transform2.mRotation, transform1.mTranslation - transform2.mTranslation)
    };

    const Vec3 c1 = Transform(transform, hull1.mCentroid);

    int maxIndex1 = -1;
    int maxIndex2 = -1;
    f32 maxSeparation = -FLT_MAX;

    const int edgeCount1 = hull1.mHalfEdgesCount;
    const int edgeCount2 = hull2.mHalfEdgesCount;
    for (int i1 = 0; i1 < edgeCount1; i1 += 2)
    {
        const ConvexHull::HalfEdge edge1 = hull1.mHalfEdges[i1];
        const ConvexHull::HalfEdge twin1 = hull1.mHalfEdges[i1 + 1];
        assert(edge1.mTwin == i1 + 1 && twin1.mTwin == i1);

        const Vec3 p1 = Transform(transform, hull1.mVertexPositions[edge1.mOrigin]);
        const Vec3 q1 = Transform(transform, hull1.mVertexPositions[twin1.mOrigin]);
        const Vec3 e1 = q1 - p1;

        const Vec3 u1 = transform.mRotation * hull1.mFacePlanes[edge1.mFace].mNormal;
        const Vec3 v1 = transform.mRotation * hull1.mFacePlanes[twin1.mFace].mNormal;

        for (int i2 = 0; i2 < edgeCount2; i2 += 2)
        {
            const ConvexHull::HalfEdge edge2 = hull2.mHalfEdges[i2];
            const ConvexHull::HalfEdge twin2 = hull2.mHalfEdges[i2 + 1];
            assert(edge2.mTwin == i2 + 1 && twin2.mTwin == i2);

            const Vec3 p2 = hull2.mVertexPositions[edge2.mOrigin];
            const Vec3 q2 = hull2.mVertexPositions[twin2.mOrigin];
            const Vec3 e2 = q2 - p2;

            const Vec3 u2 = hull2.mFacePlanes[edge2.mFace].mNormal;
            const Vec3 v2 = hull2.mFacePlanes[twin2.mFace].mNormal;

            if (IsMinkowskiFace(u1, v1, -e1, -u2, -v2, -e2))
            {
                const f32 separation = Project(p1, e1, p2, e2, c1);
                if (separation > maxSeparation)
                {
                    maxIndex1 = i1;
                    maxIndex2 = i2;
                    maxSeparation = separation;
                }
            }
        }
    }

    return {maxIndex1, maxIndex2, maxSeparation};
}

static int ClipPolygon(
    ClipVertex out[255],
    const ClipVertex in[255],
    int inCount,
    Plane plane,
    u8 planeEdgeIndex
)
{
    assert(out);
    assert(inCount >= 3);

    ClipVertex vertex1 = in[inCount - 1];
    f32 distance1 = Distance(plane, vertex1.mPosition);

    int outCount = 0;

    for (int i = 0; i < inCount; ++i)
    {
        const ClipVertex vertex2 = in[i];
        f32 distance2 = Distance(plane, vertex2.mPosition);

        if (distance1 <= 0.0f && distance2 <= 0.0f)
        {
            // Both vertices are behind the plane => keep vertex2.
            assert(outCount < 255);
            out[outCount++] = vertex2;
        }
        else if (distance1 <= 0.0f && distance2 > 0.0f)
        {
            // Vertex1 behind, vertex2 in front => intersection point.
            assert(outCount < 255);
            out[outCount].mFeatureId.mInHalfEdgeR = FeatureId::EDGE_NULL;
            out[outCount].mFeatureId.mInHalfEdgeI = vertex1.mFeatureId.mOutHalfEdgeI;
            out[outCount].mFeatureId.mOutHalfEdgeR = planeEdgeIndex;
            out[outCount].mFeatureId.mOutHalfEdgeI = FeatureId::EDGE_NULL;

            out[outCount].mPosition
                = Lerp(vertex2.mPosition, vertex1.mPosition, distance1 / (distance1 - distance2));
            ++outCount;
        }
        else if (distance2 <= 0.0f && distance1 > 0.0f)
        {
            // Vertex2 behind, vertex1 in front => intersection and vertex2.
            assert(outCount < 254);

            out[outCount].mFeatureId.mInHalfEdgeR = planeEdgeIndex;
            out[outCount].mFeatureId.mInHalfEdgeI = FeatureId::EDGE_NULL;
            out[outCount].mFeatureId.mOutHalfEdgeR = FeatureId::EDGE_NULL;
            out[outCount].mFeatureId.mOutHalfEdgeI = vertex2.mFeatureId.mOutHalfEdgeI;

            out[outCount].mPosition
                = Lerp(vertex2.mPosition, vertex1.mPosition, distance1 / (distance1 - distance2));
            ++outCount;

            out[outCount] = vertex2;
            ++outCount;
        }

        vertex1 = vertex2;
        distance1 = distance2;
    }

    return outCount;
}

static int HullBuildFaceContact(
    ContactManifold& manifold,
    const TransformMat& transform1,
    const ConvexHull& hull1,
    const TransformMat& transform2,
    const ConvexHull& hull2,
    HullFaceQuery query,
    bool flipNormal
)
{
    const u8 referenceFaceIndex = static_cast<u8>(query.mFaceIndex);
    const Plane referenceFacePlane = Transform(transform1, hull1.mFacePlanes[referenceFaceIndex]);
    const Vec3 referenceFaceNormal
        = transform1.mRotation * hull1.mFacePlanes[referenceFaceIndex].mNormal;

    const int hullFacesCount2 = hull2.mFacesCount;
    f32 minDotNormals = FLT_MAX;
    int incidentFaceIndex = -1;
    for (int i = 0; i < hullFacesCount2; ++i)
    {
        const Vec3 normal = transform2.mRotation * hull2.mFacePlanes[i].mNormal;
        const f32 dotNormals = Dot(normal, referenceFaceNormal);
        if (dotNormals < minDotNormals)
        {
            minDotNormals = dotNormals;
            incidentFaceIndex = i;
        }
    }

    assert(incidentFaceIndex > -1);

    Vec3 referenceVertices[255];
    const int referenceVerticesCount = hull1.GetVertices(referenceVertices, referenceFaceIndex);

    for (int i = 0; i < referenceVerticesCount; ++i)
    {
        referenceVertices[i] = Transform(transform1, referenceVertices[i]);
    }

    ClipVertex incidentVertices[255];
    const int incidentVerticesCount
        = hull2.GetClipVertices(incidentVertices, static_cast<u8>(incidentFaceIndex));

    for (int i = 0; i < incidentVerticesCount; ++i)
    {
        incidentVertices[i].mPosition = Transform(transform2, incidentVertices[i].mPosition);
    }

    Plane referenceSidePlanes[255];
    u8 referenceSidePlanesEdgeIndices[255];
    const int referenceSidePlanesCount = hull1.GetSidePlanes(
        referenceSidePlanes,
        referenceSidePlanesEdgeIndices,
        referenceFaceIndex
    );

    for (int i = 0; i < referenceSidePlanesCount; ++i)
    {
        referenceSidePlanes[i] = Transform(transform1, referenceSidePlanes[i]);
    }

    ClipVertex clipped[255];
    ClipVertex clipped2[255];
    static_assert(sizeof(clipped) == sizeof(clipped2));

    static_assert(sizeof(incidentVertices) == sizeof(clipped2));
    memcpy(clipped2, incidentVertices, sizeof(clipped2));

    int clippedCount = incidentVerticesCount;

    for (int i = 0; i < referenceSidePlanesCount; ++i)
    {
        clippedCount = ClipPolygon(
            clipped,
            clipped2,
            clippedCount,
            referenceSidePlanes[i],
            referenceSidePlanesEdgeIndices[i]
        );
        if (clippedCount == 0)
        {
            return 0;
        }
        memcpy(clipped2, clipped, sizeof(clipped2));
    }

    // Discard clipped points above the reference face.
    int outCount = 0;
    f32 separations[255];
    Vec3 positions[255];
    FeatureId featureIds[255];
    for (int i = 0; i < clippedCount; ++i)
    {
        const f32 d = Distance(referenceFacePlane, clipped[i].mPosition);
        if (d <= 0.0f)
        {
            separations[outCount] = d;
            positions[outCount] = clipped[i].mPosition;
            featureIds[outCount] = clipped[i].mFeatureId;
            ++outCount;
        }
    }

    for (int i = 0; i < outCount; ++i)
    {
        positions[i] -= separations[i] * referenceFaceNormal;
    }

    const Vec3 normal = flipNormal ? -referenceFaceNormal : referenceFaceNormal;

    for (int i = 0; i < outCount; ++i)
    {
        clipped[i].mPosition = positions[i];
        clipped[i].mFeatureId = featureIds[i];
    }

    if (outCount > ContactManifold::CONTACT_MAX_POINTS)
    {
        outCount
            = ReduceContactPoints(clipped, positions, featureIds, separations, outCount, normal);
    }

    assert(outCount <= ContactManifold::CONTACT_MAX_POINTS);

    if (flipNormal)
    {
        for (int i = 0; i < outCount; ++i)
        {
            clipped[i].mFeatureId.Flip();
        }
    }

    manifold.mNormal = normal;
    int contactsCount = 0;
    for (int i = 0; i < outCount; ++i)
    {
        manifold.mContacts[contactsCount].mPosition = clipped[i].mPosition;
        manifold.mContacts[contactsCount].mSeparation = separations[i];
        manifold.mContacts[contactsCount].mFeatureId = clipped[i].mFeatureId;
        ++contactsCount;
    }

    return contactsCount;
}

static int HullBuildEdgeContact(
    ContactManifold& manifold,
    const TransformMat& transform1,
    const ConvexHull& hull1,
    const TransformMat& transform2,
    const ConvexHull& hull2,
    HullEdgeQuery query
)
{
    const Vec3 p1 = Transform(transform1, hull1.GetOrigin(static_cast<u8>(query.mEdgeIndex1)));
    const Vec3 q1 = Transform(transform1, hull1.GetTarget(static_cast<u8>(query.mEdgeIndex1)));

    const Vec3 p2 = Transform(transform2, hull2.GetOrigin(static_cast<u8>(query.mEdgeIndex2)));
    const Vec3 q2 = Transform(transform2, hull2.GetTarget(static_cast<u8>(query.mEdgeIndex2)));

    const Vec3 segment1 = q1 - p1;
    const Vec3 segment2 = q2 - p2;
    const Vec3 r = p1 - p2;
    const f32 lengthSq1 = MagnitudeSq(segment1);
    const f32 lengthSq2 = MagnitudeSq(segment2);
    const f32 f = Dot(segment2, r);
    const f32 c = Dot(segment1, r);

    const f32 b = Dot(segment1, segment2);
    const f32 denominator = lengthSq1 * lengthSq2 - b * b;

    const f32 s = (b * f - c * lengthSq2) / denominator;
    const f32 t = (b * s + f) / lengthSq2;

    const Vec3 c1 = p1 + segment1 * s;
    const Vec3 c2 = p2 + segment2 * t;

    Vec3 normal = Cross(segment1, segment2);

    if (AlmostEqual(normal.X(), 0.0f) && AlmostEqual(normal.Y(), 0.0f)
        && AlmostEqual(normal.Z(), 0.0f))
    {
        return 0;
    }

    normal = Normalize(normal);

    if (Dot(normal, p1 - Transform(transform1, hull1.mCentroid)) < 0.0f)
    {
        normal = -normal;
    }

    manifold.mNormal = normal;
    manifold.mContacts[0].mPosition = (c1 + c2) / 2.0f;
    manifold.mContacts[0].mSeparation = Dot(normal, p2 - p1);
    manifold.mContacts[0].mFeatureId.mInHalfEdgeR = static_cast<u8>(query.mEdgeIndex1);
    manifold.mContacts[0].mFeatureId.mOutHalfEdgeR = FeatureId::EDGE_NULL;
    manifold.mContacts[0].mFeatureId.mInHalfEdgeI = static_cast<u8>(query.mEdgeIndex2);
    manifold.mContacts[0].mFeatureId.mOutHalfEdgeI = FeatureId::EDGE_NULL;

    return 1;
}

static void CollideSphereSphere(
    ContactManifold& manifold,
    const World& world,
    const Body& sphere1,
    const Body& sphere2
)
{
    (void)world;

    const f32 sumRadii = sphere1.mRadius + sphere2.mRadius;
    const Vec3 translation = sphere2.mPosition - sphere1.mPosition;

    const f32 tMagSq = MagnitudeSq(translation);

    if (tMagSq - sumRadii * sumRadii > 0.0f)
    {
        manifold.mContactsCount = 0;
        return;
    }

    assert(tMagSq != 0.0f);

    const f32 tMag = sqrtf(tMagSq);
    assert(tMag > 0.0f);
    const Vec3 normal = translation / tMag;
    const f32 body1ToContactMag = 0.5f * (tMag - sphere2.mRadius + sphere1.mRadius);

    manifold.mNormal = normal;
    manifold.mContacts[0].mSeparation = tMag - sphere1.mRadius - sphere2.mRadius;
    manifold.mContacts[0].mPosition = sphere1.mPosition + normal * body1ToContactMag;
    manifold.mContacts[0].mFeatureId = {};
    manifold.mContactsCount = 1;
}

static void CollideSphereConvexHull(
    ContactManifold& manifold,
    const World& world,
    const Body& sphere,
    const Body& hull
)
{
    const Slice<ConvexHull> convexHulls = world.GetConvexHulls();
    assert(hull.mConvexHull.mId < convexHulls.mCount);
    const ConvexHull& convexHull = convexHulls.mData[hull.mConvexHull.mId];
    const TransformMat hullLocalToWorld{ToMat3(hull.mOrientation), hull.mPosition};
    const Vec3 spherePositionHullLocal = InverseTransform(hullLocalToWorld, sphere.mPosition);
    const f32 sphereRadius = sphere.mRadius;

    GjkSupport support{};
    support.mA = convexHull.mVertexPositions[0];
    support.mB = spherePositionHullLocal;

    GjkSimplex simplex{};

    while (Gjk(simplex, support))
    {
        support.mIdA = GetSupportPointIndex(
            convexHull.mVertexPositions,
            convexHull.mVerticesCount,
            support.mDirectionA
        );
        support.mA = convexHull.mVertexPositions[support.mIdA];
    }

    GjkResult result{};
    GjkAnalyze(result, simplex);

    if (!result.mHit)
    {
        // Sphere center is outside the hull, deep contact is not possible.
        const Vec3 closestPointOnHull = Transform(hullLocalToWorld, result.mP0);
        const Vec3 segment = closestPointOnHull - sphere.mPosition;
        const f32 distance2 = MagnitudeSq(segment);
        if (distance2 < sphereRadius * sphereRadius)
        {
            // Shallow contact.
            const f32 distance = sqrtf(distance2);
            manifold.mNormal = segment / distance;
            manifold.mContacts[0].mPosition = closestPointOnHull;
            manifold.mContacts[0].mSeparation = distance - sphereRadius;
            manifold.mContacts[0].mFeatureId = {};
            // TODO: it seems to uniquely identify the contact but idk.
            manifold.mContacts[0].mFeatureId.mInHalfEdgeR = static_cast<u8>(support.mIdA);
            manifold.mContactsCount = 1;
            return;
        }
        else
        {
            // No contact.
            manifold.mContactsCount = 0;
            return;
        }
    }

    // Deep contact.

    // SAT only for hull faces.
    int minIndex = -1;
    f32 minDistance = -FLT_MAX;
    const int hullFacesCount = convexHull.mFacesCount;
    Plane minFace{};
    for (int i = 0; i < hullFacesCount; ++i)
    {
        const Plane plane = Transform(hullLocalToWorld, convexHull.mFacePlanes[i]);
        const f32 d = Distance(plane, sphere.mPosition);
        if (d > minDistance)
        {
            minIndex = i;
            minDistance = d;
            minFace = plane;
        }
    }
    assert(minIndex > -1);

    manifold.mNormal = -minFace.mNormal;
    manifold.mContacts[0].mPosition = ClosestPoint(minFace, sphere.mPosition);
    manifold.mContacts[0].mSeparation = minDistance - sphereRadius;
    manifold.mContacts[0].mFeatureId = {};
    // TODO: it seems to uniquely identify the contact but idk.
    manifold.mContacts[0].mFeatureId.mInHalfEdgeR = static_cast<u8>(minIndex);
    manifold.mContactsCount = 1;
}

void CollideConvexHullConvexHull(
    ContactManifold& manifold,
    const World& world,
    const Body& body1,
    const Body& body2
)
{
    const TransformMat transform1 = {ToMat3(body1.mOrientation), body1.mPosition};
    const TransformMat transform2 = {ToMat3(body2.mOrientation), body2.mPosition};

    const Slice<ConvexHull> convexHulls = world.GetConvexHulls();

    assert(body1.mConvexHull.mId < convexHulls.mCount);
    assert(body2.mConvexHull.mId < convexHulls.mCount);

    const ConvexHull& hull1 = convexHulls.mData[body1.mConvexHull.mId];
    const ConvexHull& hull2 = convexHulls.mData[body2.mConvexHull.mId];

    const HullFaceQuery faceQuery1 = HullQueryFaceDirections(transform1, transform2, hull1, hull2);

    if (faceQuery1.mSeparation > 0.0f)
    {
        manifold.mContactsCount = 0;
        return;
    }

    const HullFaceQuery faceQuery2 = HullQueryFaceDirections(transform2, transform1, hull2, hull1);

    if (faceQuery2.mSeparation > 0.0f)
    {
        manifold.mContactsCount = 0;
        return;
    }

    const HullEdgeQuery edgeQuery = HullQueryEdgeDirections(transform1, transform2, hull1, hull2);

    if (edgeQuery.mSeparation > 0.0f)
    {
        manifold.mContactsCount = 0;
        return;
    }

    constexpr f32 LINEAR_SLOP = 0.005f;
    constexpr f32 RELATIVE_EDGE_TOLERANCE = 0.90f;
    constexpr f32 RELATIVE_FACE_TOLERANCE = 0.98f;
    constexpr f32 ABSOLUTE_TOLERANCE = 0.5f * LINEAR_SLOP;

    const f32 maxFaceSeparation = Max(faceQuery1.mSeparation, faceQuery2.mSeparation);

    if (edgeQuery.mSeparation > RELATIVE_EDGE_TOLERANCE * maxFaceSeparation + ABSOLUTE_TOLERANCE)
    {
        manifold.mContactsCount
            = HullBuildEdgeContact(manifold, transform1, hull1, transform2, hull2, edgeQuery);
    }
    else
    {
        if (faceQuery2.mSeparation
            > RELATIVE_FACE_TOLERANCE * faceQuery1.mSeparation + ABSOLUTE_TOLERANCE)
        {
            // Face contact 2.
            manifold.mContactsCount = HullBuildFaceContact(
                manifold,
                transform2,
                hull2,
                transform1,
                hull1,
                faceQuery2,
                true
            );
        }
        else
        {
            // Face contact 1.
            manifold.mContactsCount = HullBuildFaceContact(
                manifold,
                transform1,
                hull1,
                transform2,
                hull2,
                faceQuery1,
                false
            );
        }
    }
}

void Collide(ContactManifold& manifold, const World& world, const Body& body1, const Body& body2)
{
    // Robust Contact Creation for Physics Simulation, Dirk Gregorius
    // https://www.gdcvault.com/play/1022193/Physics-for-Game-Programmers-Robust
    // Slides:
    // https://gdcvault.com/play/1022194/Physics-for-Game-Programmers-Robust
    // The Separating Axis Test between Convex Polyhedra, Dirk Gregorius
    // https://media.gdcvault.com/gdc2013/slides/822403Gregorius_Dirk_TheSeparatingAxisTest.pdf

    using CollideFunction = void (*const)(ContactManifold&, const World&, const Body&, const Body&);

    // TODO: capsule and triangle mesh.
    // This is an upper triangular collision functions matrix, since we swap bodies if:
    // body1.Shape > body2.Shape
    // clang-format off
    static constexpr CollideFunction sCollisionMatrix[Body::Shape::Count][Body::Shape::Count] =
    {
        {CollideSphereSphere, CollideSphereConvexHull},
        {nullptr,             CollideConvexHullConvexHull},
    };
    // clang-format on

    const bool bodyOrderOk = body1.mShape <= body2.mShape;
    const Body& b1 = bodyOrderOk ? body1 : body2;
    const Body& b2 = bodyOrderOk ? body2 : body1;
    bool flipNormal = !bodyOrderOk;

    assert(b1.mShape <= b2.mShape);

    const CollideFunction function = sCollisionMatrix[b1.mShape][b2.mShape];

    assert(function);

    function(manifold, world, b1, b2);

    if (manifold.mContactsCount > 0)
    {
        if (flipNormal)
        {
            manifold.mNormal = -manifold.mNormal;
            for (int i = 0; i < manifold.mContactsCount; ++i)
            {
                manifold.mContacts[i].mFeatureId.Flip();
            }
        }
        ComputeBasis(manifold.mNormal, manifold.mTangents[0], manifold.mTangents[1]);
    }
}
