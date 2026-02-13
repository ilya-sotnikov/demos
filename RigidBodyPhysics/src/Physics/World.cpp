#include "World.hpp"

#include "../Utils.hpp"
#include "../Arena.hpp"
#include "Config.hpp"
#include "Collide.hpp"
#include "MassProperties.hpp"
#include "../Math/Vec3.hpp"
#include "../Math/Mat3.hpp"
#include "../Math/Quat.hpp"
#include "../Math/Hash.hpp"
#include "../Renderer/Renderer.hpp"
#include "../TimeMeter.hpp"

#include "imgui.h"

void World::BroadPhaseAdd(HGrid& hgrid, HGrid::Object* obj)
{
    assert(obj);

    // Find lowest level where object fully fits inside cell.
    const f32 diameter = 2.0f * obj->mRadius;
    i16 level = 0;
    for (size_t i = 0; i < ARRAY_SIZE(HGrid::LEVEL_SIZES); ++i)
    {
        if (diameter <= HGrid::LEVEL_SIZES[i])
        {
            break;
        }
        ++level;
    }
    assert(level < static_cast<i16>(ARRAY_SIZE(HGrid::LEVEL_SIZES)));
    const f32 cellSize = HGrid::LEVEL_SIZES[level];

    // Add object to grid square, and remember cell and level numbers.
    const HGrid::Cell cell{
        static_cast<i16>(roundf(obj->mPosition.X() / cellSize)),
        static_cast<i16>(roundf(obj->mPosition.Y() / cellSize)),
        static_cast<i16>(roundf(obj->mPosition.Z() / cellSize)),
        level
    };
    const u64 bucket = Hash::Splittable64(Utils::BitCast<u64>(cell)) % HGrid::BUCKETS_COUNT;
    obj->mBucket = static_cast<int>(bucket);
    obj->mLevel = level;
    obj->mNext = hgrid.mObjectBucket[bucket];
    hgrid.mObjectBucket[bucket] = obj;

    ++hgrid.mObjectsAtLevel[level];
    hgrid.mOccupiedLevelsMask |= (1U << level);
}

void World::BroadPhaseCheck(HGrid& hgrid, const HGrid::Object* obj)
{
    assert(obj);
    assert(obj->mId != -1);

    int startLevel = 0;
    u32 occupiedLevelsMask = hgrid.mOccupiedLevelsMask;
    const Vec3 pos = obj->mPosition;
    constexpr f32 EPSILON = 0.01f;
    constexpr int LEVELS_COUNT = ARRAY_SSIZE(HGrid::LEVEL_SIZES);

    const f32 diameter = 2.0f * obj->mRadius;
    // If all objects are tested at the same time, the appropriate starting
    // grid level can be computed as:
    for (size_t i = 0; i < LEVELS_COUNT; ++i)
    {
        if (diameter <= HGrid::LEVEL_SIZES[i])
        {
            break;
        }
        occupiedLevelsMask >>= 1;
        ++startLevel;
    }
    assert(startLevel < LEVELS_COUNT);

    // For each new query, increase time stamp counter.
    ++hgrid.mTick;

    for (int level = startLevel; level < LEVELS_COUNT; ++level)
    {
        const f32 cellSize = HGrid::LEVEL_SIZES[level];

        // If no objects in rest of grid, stop now.
        if (hgrid.mOccupiedLevelsMask == 0)
        {
            break;
        }

        // If no objects at this level, go on to the next level.
        if ((occupiedLevelsMask & 1) == 0)
        {
            continue;
        }

        // Compute ranges [x1..x2, y1..y2, z1..z2] of cells overlapped on this level.
        // To make sure objects in neighboring cells are tested, by increasing range
        // by the maximum object overlap.
        const f32 delta = obj->mRadius + cellSize + EPSILON;
        const f32 inverseCellSize = 1.0f / cellSize;
        const int x1 = static_cast<int>(floorf((pos.X() - delta) * inverseCellSize));
        const int y1 = static_cast<int>(floorf((pos.Y() - delta) * inverseCellSize));
        const int z1 = static_cast<int>(floorf((pos.Z() - delta) * inverseCellSize));
        const int x2 = static_cast<int>(ceilf((pos.X() + delta) * inverseCellSize));
        const int y2 = static_cast<int>(ceilf((pos.Y() + delta) * inverseCellSize));
        const int z2 = static_cast<int>(ceilf((pos.Z() + delta) * inverseCellSize));

        // Check all the grid cells overlapped on current level.
        for (int x = x1; x <= x2; ++x)
        {
            for (int y = y1; y <= y2; ++y)
            {
                for (int z = z1; z <= z2; ++z)
                {
                    const HGrid::Cell cellPos{
                        static_cast<i16>(x),
                        static_cast<i16>(y),
                        static_cast<i16>(z),
                        static_cast<i16>(level)
                    };
                    const u64 bucket
                        = Hash::Splittable64(Utils::BitCast<u64>(cellPos)) % HGrid::BUCKETS_COUNT;

                    // Has this hash bucket already been checked for this object?
                    if (hgrid.mTimeStamp[bucket] == hgrid.mTick)
                    {
                        continue;
                    }
                    hgrid.mTimeStamp[bucket] = hgrid.mTick;

                    // Loop through all objects in the bucket to find nearby objects.
                    const HGrid::Object* o = hgrid.mObjectBucket[bucket];
                    while (o)
                    {
                        if (o != obj)
                        {
                            assert(o->mId != -1);
                            ++hgrid.mTestsCount;
                            const f32 dist2 = MagnitudeSq(pos - o->mPosition);
                            if (dist2 <= Square(obj->mRadius + o->mRadius + EPSILON))
                            {
                                NarrowPhase(obj, o);
                            }
                            else
                            {
                                // Broke the contact (or no contact at all).
                                ManifoldErase({obj->mId, o->mId});
                            }
                        }
                        o = o->mNext;
                    }
                }
            }
        }

        occupiedLevelsMask >>= 1;
    }
}

void World::NarrowPhase(const HGrid::Object* obj1, const HGrid::Object* obj2)
{
    assert(obj1);
    assert(obj2);

    const ContactManifold::Key key = {obj1->mId, obj2->mId};
    assert(Utils::BitCast<u64>(key) != UINT64_MAX);

    if (obj1->mInverseMass == 0.0f && obj2->mInverseMass == 0.0f)
    {
        return;
    }

    ContactManifold manifold{};
    ManifoldInit(manifold, obj1->mId, obj2->mId);

    if (manifold.mContactsCount > 0)
    {
        const int index = ManifoldFind(key);
        if (index == -1)
        {
            ManifoldInsert(key, manifold);
        }
        else
        {
            ManifoldUpdate(mContactManifolds[index], manifold, manifold.mContactsCount);
        }
    }
    else
    {
        // Broke the contact (or no contact at all).
        ManifoldErase(key);
    }
}

static const char* BodyShapeToString(u8 shape)
{
    switch (shape)
    {
    case Body::Shape::Sphere:
        return "Sphere";
    case Body::Shape::ConvexHull:
        return "Hull";
    default:
        return "Unknown";
    }
}

void World::BodyInitSphere(Body& body, f32 density, f32 radius) const
{
    assert(radius > 0.0f);
    assert(density > 0.0f);

    body = {};
    body.mShape = Body::Shape::Sphere;
    body.mOrientation = {1.0f, 0.0f, 0.0f, 0.0f};
    body.mFriction = 0.2f;
    body.mLinearDamping = 0.1f;
    body.mAngularDamping = 0.1f;

    body.mRadius = radius;

    MassProperties::CalculateSphere(radius, density, body.mInverseInertia, body.mInverseMass);
}

void World::BodyInitConvexHull(Body& body, f32 density, ConvexHull::Id hullId) const
{
    assert(density > 0.0f);

    body = {};
    body.mShape = Body::Shape::ConvexHull;
    body.mOrientation = {1.0f, 0.0f, 0.0f, 0.0f};
    body.mFriction = 0.2f;
    body.mLinearDamping = 0.1f;
    body.mAngularDamping = 0.1f;

    body.mConvexHull.mId = hullId;

    const ConvexHull& hull = mConvexHulls[hullId];
    Vec3 centerOfMass{};
    MassProperties::CalculatePolyhedronTriangleMesh(
        hull.mMeshPositions.mData,
        hull.mMeshIndices.mData,
        hull.mMeshIndices.mCount,
        density,
        hull.mScale,
        body.mInverseInertia,
        centerOfMass,
        body.mInverseMass
    );
    assert(AlmostEqual(centerOfMass, hull.mCentroid, 0.0001f));
    body.mRadius = hull.mRadius;
}

int World::ManifoldFind(ContactManifold::Key key) const
{
    const u64 searchKey = Utils::BitCast<u64>(key);
    assert(searchKey != UINT64_MAX);
    u64 index = Hash::Splittable64(searchKey) % PHYSICS_MAX_CONTACT_MANIFOLDS;
    const u64 originalIndex = index;

    u64 k = 0;
    do
    {
        k = Utils::BitCast<u64>(mContactManifoldsKeys[index]);
        if (k == searchKey)
        {
            return static_cast<int>(index);
        }
        index = (index + 1) % PHYSICS_MAX_CONTACT_MANIFOLDS;
    }
    while ((k != UINT64_MAX) && (index != originalIndex));

    return -1;
}

void World::ManifoldInsert(ContactManifold::Key key, const ContactManifold& manifold)
{
    assert(mContactManifoldsCount < PHYSICS_MAX_CONTACT_MANIFOLDS);
    if (mContactManifoldsCount >= PHYSICS_MAX_CONTACT_MANIFOLDS)
    {
        return;
    }

    u64 index = Hash::Splittable64(Utils::BitCast<u64>(key)) % PHYSICS_MAX_CONTACT_MANIFOLDS;
    const u64 originalIndex = index;
    (void)originalIndex;

    while (Utils::BitCast<u64>(mContactManifoldsKeys[index]) != UINT64_MAX)
    {
        index = (index + 1) % PHYSICS_MAX_CONTACT_MANIFOLDS;
        assert(index != originalIndex);
        assert(Utils::BitCast<u64>(mContactManifoldsKeys[index]) != Utils::BitCast<u64>(key));
    }

    assert(Utils::BitCast<u64>(mContactManifoldsKeys[index]) == UINT64_MAX);
    assert(Utils::BitCast<u64>(key) != UINT64_MAX);
    mContactManifoldsKeys[index] = key;
    mContactManifolds[index] = manifold;
    ++mContactManifoldsCount;
}

void World::ManifoldErase(ContactManifold::Key key)
{
    int index = ManifoldFind(key);

    if (index == -1)
    {
        return;
    }

    mContactManifoldsKeys[index] = {-1, -1};
    --mContactManifoldsCount;

    // Rehashing is necessary, since linear probing can fail to find a key
    // because of the hole we made.
    const int originalIndex = index;
    index = (index + 1) % PHYSICS_MAX_CONTACT_MANIFOLDS;
    while ((Utils::BitCast<u64>(mContactManifoldsKeys[index]) != UINT64_MAX)
           && (index != originalIndex))
    {
        const ContactManifold::Key savedKey = mContactManifoldsKeys[index];
        const ContactManifold savedManifold = mContactManifolds[index];
        mContactManifoldsKeys[index] = {-1, -1};
        --mContactManifoldsCount;
        ManifoldInsert(savedKey, savedManifold);
        index = (index + 1) % PHYSICS_MAX_CONTACT_MANIFOLDS;
    }
}

void World::Init(Vec3 gravity, f32 timeStep, int iterations)
{
    assert(iterations > 0);
    assert(timeStep > 0.0f);

    mTimeStep = timeStep;
    mGravity = gravity;
    mIterationsCount = iterations;

    mBodies = gArenaReset.AllocOrDie<Body>(PHYSICS_MAX_BODIES);
    mInverseInertiasLocal = gArenaReset.AllocOrDie<Mat3>(PHYSICS_MAX_BODIES);

    mContactManifolds = gArenaReset.AllocOrDie<ContactManifold>(PHYSICS_MAX_CONTACT_MANIFOLDS);

    mContactManifoldsKeys = gArenaReset.AllocOrDie<ContactManifold::Key>(
        PHYSICS_MAX_CONTACT_MANIFOLDS,
        Arena::FlagNoZero
    );
    memset(
        mContactManifoldsKeys,
        0xff,
        PHYSICS_MAX_CONTACT_MANIFOLDS * sizeof(mContactManifoldsKeys[0])
    );

    mConvexHulls = gArenaReset.AllocOrDie<ConvexHull>(PHYSICS_MAX_CONVEX_HULLS);
}

ConvexHull::Id World::AddConvexHull(const ConvexHull& hull)
{
    const int id = mConvexHullsCount;
    mConvexHulls[id] = hull;
    ++mConvexHullsCount;
    return id;
}

void World::BroadPhase()
{
#ifdef PHYSICS_NO_BROADPHASE
    for (int i = 0; i < mBodiesCount; ++i)
    {
        Body& bi = mBodies[i];

        for (int j = i + 1; j < mBodiesCount; ++j)
        {
            Body& bj = mBodies[j];
            const ContactManifold::Key key = {i, j};

            if (bi.mInverseMass == 0.0f && bj.mInverseMass == 0.0f)
            {
                continue;
            }

            ContactManifold manifold{};
            ManifoldInit(manifold, i, j);

            if (manifold.mContactsCount > 0)
            {
                const int index = ManifoldFind(key);
                if (index == -1)
                {
                    ManifoldInsert(key, manifold);
                }
                else
                {
                    ManifoldUpdate(
                        mContactManifolds[index],
                        manifold.mContacts,
                        manifold.mContactsCount
                    );
                }
            }
            else
            {
                // Broke the contact (or no contact at all).
                ManifoldErase(key);
            }
        }
    }
#else
    gTimeMeters[TimeMeter::PhysicsCreateHGrid].Start();
    mHGrid = {};
    HGrid::Object* const objects = gArenaFrame.AllocOrDie<HGrid::Object>(mBodiesCount - 1);
    const int bodiesCount = mBodiesCount - 1; // Without the floor.
    for (int i = 0; i < bodiesCount; ++i)
    {
        const Body& b = mBodies[i + 1];
        objects[i].mPosition = b.mPosition;
        objects[i].mRadius = b.mRadius;
        objects[i].mInverseMass = b.mInverseMass;
        objects[i].mId = b.mId;
        BroadPhaseAdd(mHGrid, &objects[i]);
    }
    gTimeMeters[TimeMeter::PhysicsCreateHGrid].End();

    // for (int i = 0; i < HGrid::BUCKETS_COUNT; ++i)
    // {
    //     const HGrid::Object* o = mHGrid.mObjectBucket[i];
    //     while (o)
    //     {
    //         const f32 size = HGrid::LEVEL_SIZES[o->mLevel];
    //         // clang-format off
    //         const Vec3 pos = {
    //             roundf(o->mPosition.X() / size) * size,
    //             roundf(o->mPosition.Y() / size) * size,
    //             roundf(o->mPosition.Z() / size) * size
    //         };
    //         // clang-format on
    //         gRenderer.DrawBox(pos, Quat{1.0f, 0.0f, 0.0f, 0.0f}, Vec3{size}, {255, 0, 0});
    //         o = o->mNext;
    //     }
    // }

    for (int i = 0; i < bodiesCount; ++i)
    {
        BroadPhaseCheck(mHGrid, &objects[i]);
    }

    HGrid::Object floor{};
    for (int i = 0; i < bodiesCount; ++i)
    {
        NarrowPhase(&objects[i], &floor);
    }
#endif
}

Body::Id World::AddBody(const Body& body)
{
    const int id = mBodiesCount;
    if (id >= PHYSICS_MAX_BODIES)
    {
        return World::BODY_ID_INVALID;
    }

    mBodies[id] = body;
    mBodies[id].mId = id;
    mInverseInertiasLocal[id] = body.mInverseInertia;
    ++mBodiesCount;

    return id;
}

Body::Id World::SetFloor(const Body& floor)
{
    assert(mBodiesCount == 0);
    if (mBodiesCount != 0)
    {
        return World::BODY_ID_INVALID;
    }

    mBodies[0] = floor;
    mBodies[0].mId = 0;
    ++mBodiesCount;

    return 0;
}

bool World::IsBodyIdValid(Body::Id bodyId) const
{
    return bodyId >= 0 && bodyId < mBodiesCount;
}

void World::Step()
{
    const f32 timeStep = mTimeStep;
    const f32 inverseTimeStep = 1.0f / mTimeStep;

    gTimeMeters[TimeMeter::PhysicsContactManifold].Start();
    BroadPhase();
    gTimeMeters[TimeMeter::PhysicsContactManifold].End();

#ifdef PHYSICS_COLLIDE_ONLY
    return;
#endif

    gTimeMeters[TimeMeter::PhysicsInertiasWorld].Start();
    for (int i = 0; i < mBodiesCount; ++i)
    {
        Body& b = mBodies[i];
        const Mat3 rot = ToMat3(b.mOrientation);
        b.mInverseInertia = rot * mInverseInertiasLocal[i] * Transpose(rot);
    }
    gTimeMeters[TimeMeter::PhysicsInertiasWorld].End();

    gTimeMeters[TimeMeter::PhysicsIntegrateForces].Start();
    for (int i = 0; i < mBodiesCount; ++i)
    {
        Body& b = mBodies[i];

        if (b.mInverseMass == 0.0f)
        {
            continue;
        }

        b.mVelocity += (mGravity + b.mForce * b.mInverseMass) * timeStep;
        b.mAngularVelocity += (b.mInverseInertia * b.mTorque) * timeStep;

        // Damping logic is from box2d.
        // Differential equation: dv/dt + c * v = 0
        // Solution: v(t) = v0 * exp(-c * t)
        //
        // Time step: v(t + dt) = v0 * exp(-c * (t + dt)) =
        // = v0 * exp(-c * t) * exp(-c * dt) = v(t) * exp(-c * dt)
        //
        // v2 = exp(-c * dt) * v1
        //
        // Pade approximation:
        // v2 = v1 * 1 / (1 + c * dt)
        b.mVelocity *= 1.0f / (1.0f + timeStep * b.mLinearDamping);
        b.mAngularVelocity *= 1.0f / (1.0f + timeStep * b.mAngularDamping);
    }
    gTimeMeters[TimeMeter::PhysicsIntegrateForces].End();

    int manifoldsIndices[PHYSICS_MAX_CONTACT_MANIFOLDS];
    int manifoldsCount = 0;
    for (int i = 0; i < PHYSICS_MAX_CONTACT_MANIFOLDS; ++i)
    {
        if (Utils::BitCast<u64>(mContactManifoldsKeys[i]) != UINT64_MAX)
        {
            manifoldsIndices[manifoldsCount++] = i;
        }
    }
    assert(manifoldsCount == mContactManifoldsCount);

    gTimeMeters[TimeMeter::PhysicsPrestep].Start();
    for (int i = 0; i < manifoldsCount; ++i)
    {
        const int index = manifoldsIndices[i];
        ManifoldPrestep(mContactManifoldsKeys[index], mContactManifolds[index], inverseTimeStep);
    }
    gTimeMeters[TimeMeter::PhysicsPrestep].End();

    gTimeMeters[TimeMeter::PhysicsApplyImpulse].Start();
    const int iterationsCount = mIterationsCount;
    for (int iteration = 0; iteration < iterationsCount; ++iteration)
    {
        for (int i = 0; i < manifoldsCount; ++i)
        {
            const int index = manifoldsIndices[i];
            ManifoldApplyImpulse(mContactManifoldsKeys[index], mContactManifolds[index]);
        }
    }
    gTimeMeters[TimeMeter::PhysicsApplyImpulse].End();

    gTimeMeters[TimeMeter::PhysicsIntegrateVelocities].Start();
    for (int i = 0; i < mBodiesCount; ++i)
    {
        Body& b = mBodies[i];

        b.mPosition += b.mVelocity * timeStep;

        Quat angVelDt = ToQuat(b.mAngularVelocity * timeStep);
        angVelDt = angVelDt * b.mOrientation;
        b.mOrientation.W() += angVelDt.W() * 0.5f;
        b.mOrientation.X() += angVelDt.X() * 0.5f;
        b.mOrientation.Y() += angVelDt.Y() * 0.5f;
        b.mOrientation.Z() += angVelDt.Z() * 0.5f;
        b.mOrientation = Normalize(b.mOrientation);

        Clear(b.mForce);
        Clear(b.mTorque);
    }
    gTimeMeters[TimeMeter::PhysicsIntegrateVelocities].End();
}

void World::Reset()
{
    gArenaReset.FreeAll();
    *this = {};
}

void World::SetTimestep(f32 timeStep)
{
    mTimeStep = timeStep;
}

Vec3 World::GetPosition(Body::Id bodyId) const
{
    return mBodies[bodyId].mPosition;
}

void World::SetPosition(Body::Id bodyId, Vec3 position)
{
    mBodies[bodyId].mPosition = position;
}

Quat World::GetOrientation(Body::Id bodyId) const
{
    return mBodies[bodyId].mOrientation;
}

Vec3 World::GetScale(Body::Id bodyId) const
{
    const Body& body = mBodies[bodyId];
    assert(body.mShape == Body::Shape::ConvexHull);
    return mConvexHulls[body.mConvexHull.mId].mScale;
}

f32 World::GetRadius(Body::Id bodyId) const
{
    const Body& body = mBodies[bodyId];
    return body.mRadius;
}

#ifdef PHYSICS_DEBUG

void World::DebugDraw(bool drawSpheres, bool drawContacts) const
{
    const Color BODIES_COLOR = {150, 150, 150};
    constexpr Color COLD_CONTACT_COLOR = {255, 0, 0};
    constexpr Color WARM_CONTACT_COLOR = {0, 255, 0};
    constexpr Color NORMAL_AND_TANGENTS_COLOR = {255, 0, 0};
    constexpr f32 CONTACT_POINT_SIZE = 0.1f;

    if (drawSpheres)
    {
        // Without the floor.
        for (int i = 1; i < mBodiesCount; ++i)
        {
            const Body& b = mBodies[i];
            gRenderer.DrawSphere(b.mPosition, b.mOrientation, b.mRadius, BODIES_COLOR);
        }
    }

    if (drawContacts)
    {
        for (int i = 0; i < PHYSICS_MAX_CONTACT_MANIFOLDS; ++i)
        {
            if (Utils::BitCast<u64>(mContactManifoldsKeys[i]) == UINT64_MAX)
            {
                continue;
            }
            const ContactManifold& m = mContactManifolds[i];
            for (int j = 0; j < m.mContactsCount; ++j)
            {
                const ContactPoint& c = m.mContacts[j];
                const Color color = c.mIsWarmStarted ? WARM_CONTACT_COLOR : COLD_CONTACT_COLOR;
                gRenderer.DrawPoint(c.mPosition, CONTACT_POINT_SIZE, color);

                gRenderer.DrawLineOrigin(c.mPosition, m.mNormal, NORMAL_AND_TANGENTS_COLOR);

                for (int k = 0; k < 2; ++k)
                {
                    gRenderer
                        .DrawLineOrigin(c.mPosition, m.mTangents[k], NORMAL_AND_TANGENTS_COLOR);
                }
            }
        }
    }
}

int World::GetBodiesCount() const
{
    return mBodiesCount;
}

int World::GetContactManifoldsCount() const
{
    return mContactManifoldsCount;
}

const HGrid& World::GetHGrid() const
{
    return mHGrid;
}

const Slice<ConvexHull> World::GetConvexHulls() const
{
    return Slice<ConvexHull>{mConvexHulls, mConvexHullsCount};
}

void World::DebugPrintBodiesInfo() const
{
    ImGui::Begin("Physics bodies info");
    DEFER(ImGui::End());

    ImGui::Text("bodies (count = %d)", mBodiesCount);
    for (int i = 0; i < mBodiesCount; ++i)
    {
        const Body& b = mBodies[i];
        ImGui::Text("pos = %.3f %.3f %.3f", b.mPosition.X(), b.mPosition.Y(), b.mPosition.Z());
    }

    ImGui::Text("manifolds (count = %d)", mContactManifoldsCount);
    for (int i = 0; i < mContactManifoldsCount; ++i)
    {
        if (Utils::BitCast<u64>(mContactManifoldsKeys[i]) == UINT64_MAX)
        {
            continue;
        }
        const ContactManifold::Key k = mContactManifoldsKeys[i];
        const ContactManifold& m = mContactManifolds[i];
        const Body::Id id1 = k.mBodyId1;
        const Body::Id id2 = k.mBodyId2;
        ImGui::Text(
            "%s - %s (%d - %d)",
            BodyShapeToString(mBodies[id1].mShape),
            BodyShapeToString(mBodies[id2].mShape),
            id1,
            id2
        );
        for (int j = 0; j < m.mContactsCount; ++j)
        {
            const ContactPoint& p = m.mContacts[j];
            ImGui::Text("%d:%d", i, j);
            ImGui::Text(" pos = %.3f %.3f %.3f", p.mPosition.X(), p.mPosition.Y(), p.mPosition.Z());
            ImGui::Text(" separation = %f", p.mSeparation);
            ImGui::Text(" bias = %f", p.mBias);
            ImGui::Text(
                " features = R: %u %u | I: %u %u",
                p.mFeatureId.mInHalfEdgeR,
                p.mFeatureId.mOutHalfEdgeR,
                p.mFeatureId.mInHalfEdgeI,
                p.mFeatureId.mOutHalfEdgeI
            );
            ImGui::Text(
                " impulse normal = %f, tangent = %f, %f",
                p.mImpulseNormal,
                p.mImpulseTangent[0],
                p.mImpulseTangent[1]
            );
        }
        ImGui::Separator();
    }
}

#endif

void World::ManifoldInit(ContactManifold& manifold, Body::Id bodyId1, Body::Id bodyId2) const
{
    assert(bodyId1 > -1);
    assert(bodyId2 > -1);

    const Body& body1 = mBodies[bodyId1];
    const Body& body2 = mBodies[bodyId2];

    Collide(manifold, *this, body1, body2);
    manifold.mFriction = sqrtf(body1.mFriction * body2.mFriction);
}

void World::ManifoldPrestep(
    ContactManifold::Key key,
    ContactManifold& manifold,
    f32 inverseTimeStep
) const
{
    assert(inverseTimeStep > 0.0f);

    constexpr f32 ALLOWED_PENETRATION = 0.05f;
    constexpr f32 BIAS_FACTOR = 0.2f; // For Baumgarte stabilization.

    // Derivation of normal/tangent masses is explained here:
    // https://danielchappuis.ch/download/ConstraintsDerivationRigidBody3D.pdf

    Body& body1 = mBodies[key.mBodyId1];
    Body& body2 = mBodies[key.mBodyId2];

    const f32 sumInvMass = body1.mInverseMass + body2.mInverseMass;

    for (int i = 0; i < manifold.mContactsCount; ++i)
    {
        ContactPoint* const c = manifold.mContacts + i;

        const Vec3 r1 = c->mPosition - body1.mPosition;
        const Vec3 r2 = c->mPosition - body2.mPosition;

        // Precompute normal mass.
        const Vec3 crossR1Normal = Cross(r1, manifold.mNormal);
        const Vec3 crossR2Normal = Cross(r2, manifold.mNormal);
        const f32 kNormal1 = Dot(crossR1Normal, body1.mInverseInertia * crossR1Normal);
        const f32 kNormal2 = Dot(crossR2Normal, body2.mInverseInertia * crossR2Normal);
        const f32 kNormal = sumInvMass + kNormal1 + kNormal2;
        assert(kNormal != 0.0f);
        c->mMassNormal = 1.0f / kNormal;

        // Precompute tangent mass.
        for (int j = 0; j < 2; ++j)
        {
            const Vec3 crossR1Tangent = Cross(r1, manifold.mTangents[j]);
            const Vec3 crossR2Tangent = Cross(r2, manifold.mTangents[j]);
            const f32 kTangent1 = Dot(crossR1Tangent, body1.mInverseInertia * crossR1Tangent);
            const f32 kTangent2 = Dot(crossR2Tangent, body2.mInverseInertia * crossR2Tangent);
            const f32 kTangent = sumInvMass + kTangent1 + kTangent2;
            assert(kTangent != 0.0f);
            c->mMassTangent[j] = 1.0f / kTangent;
        }

        c->mBias = -BIAS_FACTOR * inverseTimeStep * Min(0.0f, c->mSeparation + ALLOWED_PENETRATION);

        const Vec3 impulse = manifold.mNormal * c->mImpulseNormal
            + manifold.mTangents[0] * c->mImpulseTangent[0]
            + manifold.mTangents[1] * c->mImpulseTangent[1];

        body1.mVelocity -= impulse * body1.mInverseMass;
        body1.mAngularVelocity -= body1.mInverseInertia * Cross(r1, impulse);

        body2.mVelocity += impulse * body2.mInverseMass;
        body2.mAngularVelocity += body2.mInverseInertia * Cross(r2, impulse);
    }
}

void World::ManifoldApplyImpulse(ContactManifold::Key key, ContactManifold& manifold) const
{
    Body& b1 = mBodies[key.mBodyId1];
    Body& b2 = mBodies[key.mBodyId2];
    for (int i = 0; i < manifold.mContactsCount; ++i)
    {
        ContactPoint* const c = manifold.mContacts + i;

        c->mBody1ToPosition = c->mPosition - b1.mPosition;
        c->mBody2ToPosition = c->mPosition - b2.mPosition;

        Vec3 relativeVelocity = b2.mVelocity + Cross(b2.mAngularVelocity, c->mBody2ToPosition)
            - b1.mVelocity - Cross(b1.mAngularVelocity, c->mBody1ToPosition);

        const f32 relativeVelocityNormal = Dot(relativeVelocity, manifold.mNormal);

        f32 impulseNormalMag = c->mMassNormal * (-relativeVelocityNormal + c->mBias);

        const f32 impulseNormalMag0 = c->mImpulseNormal;
        c->mImpulseNormal = Max(impulseNormalMag0 + impulseNormalMag, 0.0f);
        impulseNormalMag = c->mImpulseNormal - impulseNormalMag0;

        const Vec3 impulseNormal = manifold.mNormal * impulseNormalMag;

        b1.mVelocity -= impulseNormal * b1.mInverseMass;
        b1.mAngularVelocity -= b1.mInverseInertia * Cross(c->mBody1ToPosition, impulseNormal);

        b2.mVelocity += impulseNormal * b2.mInverseMass;
        b2.mAngularVelocity += b2.mInverseInertia * Cross(c->mBody2ToPosition, impulseNormal);

        relativeVelocity = b2.mVelocity + Cross(b2.mAngularVelocity, c->mBody2ToPosition)
            - b1.mVelocity - Cross(b1.mAngularVelocity, c->mBody1ToPosition);

        for (int j = 0; j < 2; ++j)
        {
            f32 impulseTangentMag
                = c->mMassTangent[j] * (-Dot(relativeVelocity, manifold.mTangents[j]));
            const f32 maxImpulseTangent = manifold.mFriction * c->mImpulseNormal;
            const f32 oldImpulseTangent = c->mImpulseTangent[j];
            c->mImpulseTangent[j] = Clamp(
                oldImpulseTangent + impulseTangentMag,
                -maxImpulseTangent,
                maxImpulseTangent
            );
            impulseTangentMag = c->mImpulseTangent[j] - oldImpulseTangent;

            const Vec3 impulseTangent = manifold.mTangents[j] * impulseTangentMag;

            b1.mVelocity -= impulseTangent * b1.mInverseMass;
            b1.mAngularVelocity -= b1.mInverseInertia * Cross(c->mBody1ToPosition, impulseTangent);

            b2.mVelocity += impulseTangent * b2.mInverseMass;
            b2.mAngularVelocity += b2.mInverseInertia * Cross(c->mBody2ToPosition, impulseTangent);
        }
    }
}

void World::ManifoldUpdate(
    ContactManifold& manifold,
    const ContactManifold& newManifold,
    int newContactsCount
) const
{
    ContactPoint mergedContacts[ContactManifold::CONTACT_MAX_POINTS];

    manifold.mContactsCount = newContactsCount;
    manifold.mNormal = newManifold.mNormal;
    manifold.mTangents[0] = newManifold.mTangents[0];
    manifold.mTangents[1] = newManifold.mTangents[1];

    assert(newContactsCount == manifold.mContactsCount);

    for (int i = 0; i < newContactsCount; ++i)
    {
        const ContactPoint* const cNew = newManifold.mContacts + i;
        int k = -1;
        for (int j = 0; j < manifold.mContactsCount; ++j)
        {
            const ContactPoint* const cOld = manifold.mContacts + j;
            if (Utils::BitCast<u32>(cNew->mFeatureId) == Utils::BitCast<u32>(cOld->mFeatureId))
            {
                k = j;
                break;
            }
        }

        if (k > -1)
        {
            ContactPoint* const c = mergedContacts + i;
            const ContactPoint* const cOld = manifold.mContacts + k;
            *c = *cNew;
#ifndef PHYSICS_COLLIDE_ONLY
            c->mImpulseNormal = cOld->mImpulseNormal;
            c->mImpulseTangent[0] = cOld->mImpulseTangent[0];
            c->mImpulseTangent[1] = cOld->mImpulseTangent[1];
#endif

#ifdef PHYSICS_DEBUG
            c->mIsWarmStarted = true;
#endif
        }
        else
        {
            mergedContacts[i] = newManifold.mContacts[i];
        }
    }

    for (int i = 0; i < newContactsCount; ++i)
    {
        manifold.mContacts[i] = mergedContacts[i];
    }
}
