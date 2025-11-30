#pragma once

#include "../Common.hpp"

#include "Geometry.hpp"
#include "Config.hpp"

struct Body
{
    using Id = int;

    struct Shape // Not enum class to avoid casts (used as index).
    {
        enum : u8
        {
            Sphere,
            ConvexHull,
            Count,
        };
    };

    Mat3 mInverseInertia;

    Quat mOrientation;

    Vec3 mPosition;
    Vec3 mVelocity;
    Vec3 mAngularVelocity;
    Vec3 mForce;
    Vec3 mTorque;

    // Bounding sphere radius.
    // TODO: implement this:
    // Smallest Enclosing Spheres, Nicolas Capens
    // https://www.flipcode.com/archives/Smallest_Enclosing_Spheres.shtml
    f32 mRadius;

    // Per-shape data.
    struct ConvexHullData
    {
        ConvexHull::Id mId;
    };
    union
    {
        ConvexHullData mConvexHull;
        // ...
    };
    Body::Id mId;

    f32 mFriction;
    f32 mLinearDamping;
    f32 mAngularDamping;
    f32 mInverseMass;

    u8 mShape;
};

struct ContactPoint
{
    Vec3 mPosition;
    Vec3 mBody1ToPosition;
    Vec3 mBody2ToPosition;
    f32 mSeparation;
    f32 mMassNormal;
    f32 mMassTangent[2];
    f32 mBias;
    f32 mImpulseNormal;
    f32 mImpulseTangent[2];
    FeatureId mFeatureId;
#ifdef PHYSICS_DEBUG
    bool mIsWarmStarted;
#endif
};

struct ContactManifold
{
    // 4 points are enough for a stable contact manifold in 3D.
    static constexpr int CONTACT_MAX_POINTS = 4;

    struct Key
    {
        Body::Id mBodyId1;
        Body::Id mBodyId2;
    };

    ContactPoint mContacts[CONTACT_MAX_POINTS];
    Vec3 mNormal;
    Vec3 mTangents[2];
    int mContactsCount;
    f32 mFriction;
};

// Real-Time Collision Detection, Christer Ericson.
struct HGrid
{
    static constexpr int BUCKETS_COUNT = PHYSICS_MAX_BODIES * 4;
    static constexpr f32 LEVEL_SIZES[] = {0.4f, 4.0f};

    struct Cell
    {
        i16 mX; // Grid indices.
        i16 mY;
        i16 mZ;
        i16 mLevel; // Grid level.
    };
    static_assert(sizeof(Cell) == 8);

    struct Object
    {
        Object* mNext; // Embedded link to next hgrid object.
        Vec3 mPosition; // Bounding sphere center.
        f32 mRadius; // Bounding sphere radius.
        int mBucket; // Index of hash bucket object is in.
        int mLevel; // Grid level for the object.
        f32 mInverseMass;
        Body::Id mId;
    };

    u32 mOccupiedLevelsMask;
    int mObjectsAtLevel[ARRAY_SIZE(LEVEL_SIZES)];
    Object* mObjectBucket[BUCKETS_COUNT];
    int mTimeStamp[BUCKETS_COUNT];
    int mTick;
    int mTestsCount;
};

// TODO: honestly this API design is kind of messed up but I can't be arsed.
struct World
{
    void Init(Vec3 gravity, f32 timeStep, int iterations);
    ConvexHull::Id AddConvexHull(const ConvexHull& hull);
    void BodyInitSphere(Body& body, f32 density, f32 radius) const;
    void BodyInitConvexHull(Body& body, f32 density, ConvexHull::Id hullId) const;
    Body::Id AddBody(const Body& body);
    Body::Id SetFloor(const Body& floor);
    bool IsBodyIdValid(Body::Id bodyId) const;
    void Step();
    void Reset();
    void SetTimestep(f32 timeStep);

#ifdef PHYSICS_DEBUG
    void DebugDraw(bool drawSpheres, bool drawContacts) const;
    void DebugPrintBodiesInfo() const;
#endif

    int GetBodiesCount() const;
    int GetContactManifoldsCount() const;
    const HGrid& GetHGrid() const;
    const Slice<ConvexHull> GetConvexHulls() const;

    // Treating Body::Id as an opaque handle, requires accessors
    // but allows to freely mess with the memory, since we don't
    // give user pointers/references.
    Vec3 GetPosition(Body::Id bodyId) const;
    void SetPosition(Body::Id bodyId, Vec3 position);
    Quat GetOrientation(Body::Id bodyId) const;
    Vec3 GetScale(Body::Id bodyId) const;
    f32 GetRadius(Body::Id bodyId) const;
    // ...

private:
    static constexpr Body::Id BODY_ID_INVALID = -1;

    HGrid mHGrid;
    Body* mBodies;
    Mat3* mInverseInertiasLocal;
    int mBodiesCount;
    ContactManifold* mContactManifolds;
    ContactManifold::Key* mContactManifoldsKeys;
    int mContactManifoldsCount;
    Vec3 mGravity;
    int mIterationsCount;
    f32 mTimeStep;

    ConvexHull* mConvexHulls;
    int mConvexHullsCount;

    void ManifoldInit(ContactManifold& manifold, Body::Id bodyId1, Body::Id bodyId2) const;
    void ManifoldPrestep(
        ContactManifold::Key key,
        ContactManifold& manifold,
        f32 inverseTimeStep
    ) const;
    void ManifoldApplyImpulse(ContactManifold::Key key, ContactManifold& manifold) const;
    void ManifoldUpdate(
        ContactManifold& manifold,
        const ContactManifold& newManifold,
        int newContactsCount
    ) const;

    int ManifoldFind(ContactManifold::Key key) const;
    void ManifoldInsert(ContactManifold::Key key, const ContactManifold& manifold);
    void ManifoldErase(ContactManifold::Key key);
    void BroadPhase();
    void NarrowPhase(const HGrid::Object* obj1, const HGrid::Object* obj2);

    void BroadPhaseAdd(HGrid& hgrid, HGrid::Object* obj);
    void BroadPhaseCheck(HGrid& hgrid, const HGrid::Object* obj);
};
