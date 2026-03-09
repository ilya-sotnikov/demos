#pragma once

#include "Common.hpp"

// exponential moving average
struct TimeMeter
{
    enum
    {
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
