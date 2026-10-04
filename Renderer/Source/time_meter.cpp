#include "time_meter.hpp"

#include "SDL3/SDL_timer.h"

// static constexpr f64 ALPHA = 0.02;
static constexpr f64 ALPHA = 0.3;
static constexpr f64 ONE_MINUS_ALPHA = 1.0 - ALPHA;
static const f64 COUNTER_PERIOD = 1.0 / f64(SDL_GetPerformanceFrequency());

void TimeMeter::start() {
    m_start_time = SDL_GetPerformanceCounter();
}

void TimeMeter::end() {
    m_end_time = SDL_GetPerformanceCounter();
    m_average_time = (ALPHA * f64(m_end_time - m_start_time) * COUNTER_PERIOD)
        + (ONE_MINUS_ALPHA * m_average_time);
}

void TimeMeter::measure_between() {
    m_end_time = SDL_GetPerformanceCounter();
    m_average_time = (ALPHA * f64(m_end_time - m_start_time) * COUNTER_PERIOD)
        + (ONE_MINUS_ALPHA * m_average_time);
    m_start_time = m_end_time;
}

f64 TimeMeter::get_us() const {
    return m_average_time * 1000'000.0;
}

f64 TimeMeter::get_ms() const {
    return m_average_time * 1000.0;
}
