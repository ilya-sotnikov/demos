#include "TimeMeter.hpp"

#include "SDL3/SDL_timer.h"

static constexpr f64 ALPHA = 0.02;
static constexpr f64 ONE_MINUS_ALPHA = 1.0 - ALPHA;
static const f64 COUNTER_PERIOD = 1.0 / static_cast<f64>(SDL_GetPerformanceFrequency());

void TimeMeter::Start()
{
    mStartTime = SDL_GetPerformanceCounter();
}

void TimeMeter::End()
{
    mEndTime = SDL_GetPerformanceCounter();
    mAverageTime = (ALPHA * static_cast<f64>(mEndTime - mStartTime) * COUNTER_PERIOD)
        + (ONE_MINUS_ALPHA * mAverageTime);
}

void TimeMeter::MeasureBetween()
{
    mEndTime = SDL_GetPerformanceCounter();
    mAverageTime = (ALPHA * static_cast<f64>(mEndTime - mStartTime) * COUNTER_PERIOD)
        + (ONE_MINUS_ALPHA * mAverageTime);
    mStartTime = mEndTime;
}

f64 TimeMeter::GetUs() const
{
    return mAverageTime * 1000'000.0;
}

f64 TimeMeter::GetMs() const
{
    return mAverageTime * 1000.0;
}
