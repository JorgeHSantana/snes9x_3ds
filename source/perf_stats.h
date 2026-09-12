#ifndef PERF_STATS_H
#define PERF_STATS_H

#include <stdint.h>

static inline uint64_t ticks_to_microseconds(uint64_t ticks, uint32_t ticks_per_second)
{
    static constexpr uint64_t MICROSECONDS_PER_SECOND = 1000000;
    if (ticks_per_second == 0) {
        return 0;
    }
    const uint64_t whole_seconds = ticks / ticks_per_second;
    if (whole_seconds > UINT64_MAX / MICROSECONDS_PER_SECOND) {
        return UINT64_MAX;
    }
    const uint64_t remainder = ticks % ticks_per_second;
    return whole_seconds * MICROSECONDS_PER_SECOND +
           remainder * MICROSECONDS_PER_SECOND / ticks_per_second;
}

// Fixed-storage timing aggregate for infrequent operations. Callers retain
// ownership and choose when to report/reset; recording never performs I/O.
struct TimingStats
{
    uint64_t total_ticks = 0;
    uint64_t max_ticks = 0;
    uint32_t samples = 0;

    void record(uint64_t ticks)
    {
        if (ticks > max_ticks) {
            max_ticks = ticks;
        }
        if (samples == UINT32_MAX) {
            return;
        }
        if (UINT64_MAX - total_ticks < ticks) {
            total_ticks = UINT64_MAX;
        } else {
            total_ticks += ticks;
        }
        ++samples;
    }

    uint32_t sample_count() const
    {
        return samples;
    }

    uint64_t average_ticks() const
    {
        return samples == 0 ? 0 : total_ticks / samples;
    }

    uint64_t maximum_ticks() const
    {
        return max_ticks;
    }

    void reset()
    {
        total_ticks = 0;
        max_ticks = 0;
        samples = 0;
    }
};

#endif
