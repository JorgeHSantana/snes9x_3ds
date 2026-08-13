#pragma once
#include "3dsmsu.h"
#include <cstring>

namespace fake {
inline uint32_t queued, cleared, inited, shutdowns;
inline float    last_mix;
inline uint32_t free_bufs;                 // test controls this
inline int16_t  last_samples[1024];
inline uint32_t last_sample_count;
constexpr uint32_t TOTAL_BUFS = 4;
constexpr uint32_t CAP_SAMPLES = 64;

inline void reset()
{
    queued = cleared = inited = shutdowns = 0;
    last_mix = -1.0f; free_bufs = TOTAL_BUFS;
    last_sample_count = 0;
    memset(last_samples, 0, sizeof(last_samples));
}
inline bool init_channel(uint32_t) { inited++; return true; }
inline void set_mix(float v) { last_mix = v; }
inline bool queue_buffer(const int16_t* s, uint32_t n)
{
    if (free_bufs == 0) { return false; }
    free_bufs--; queued++;
    last_sample_count = n;
    memcpy(last_samples, s, n * 4 > sizeof(last_samples) ? sizeof(last_samples) : n * 4);
    return true;
}
inline uint32_t free_buffer_count(void) { return free_bufs; }
inline uint32_t total_buffer_count(void) { return TOTAL_BUFS; }
inline uint32_t buffer_capacity_samples(void) { return CAP_SAMPLES; }
inline void clear_queue(void) { cleared++; free_bufs = TOTAL_BUFS; }
inline void shutdown_channel(void) { shutdowns++; }

inline Msu1AudioBackend make()
{
    return { init_channel, set_mix, queue_buffer, free_buffer_count,
             total_buffer_count, buffer_capacity_samples, clear_queue, shutdown_channel };
}
} // namespace fake
