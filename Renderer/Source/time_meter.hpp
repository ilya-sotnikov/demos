#pragma once

#include "Common.hpp"

// Exponential moving average.
struct TimeMeter {
    enum { FRAME, COUNT };

    u64 m_start_time;
    u64 m_end_time;
    f64 m_average_time;

    void start();
    void end();
    void measure_between(); // uses only 1 get_time function
    f64 get_us() const;
    f64 get_ms() const;
};

inline TimeMeter g_time_meters[TimeMeter::COUNT];
