#pragma once

#include "Common.hpp"

// exponential moving average
struct TimeMeter
{
    enum
    {
        ProcessEvents,
        ProcessInput,
        Physics,
        PhysicsCreateHGrid,
        PhysicsContactManifold,
        PhysicsInertiasWorld,
        PhysicsIntegrateForces,
        PhysicsPrestep,
        PhysicsApplyImpulse,
        PhysicsIntegrateVelocities,
        NewFrameFence,
        UpdateShadowCascades,
        UiDraw,
        Frame,
        Count
    };

    u64 mStartTime;
    u64 mEndTime;
    f64 mAverageTime;

    void Start();
    void End();
    void MeasureBetween(); // uses only 1 getTime function
    f64 GetUs() const;
    f64 GetMs() const;
};

inline TimeMeter gTimeMeters[TimeMeter::Count];
