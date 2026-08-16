#ifndef _3DSREWINDMETER_H
#define _3DSREWINDMETER_H

#include <stdint.h>

// Replay-speed meter (rewind v2, docs/rewind-v2-spec.md section A.3):
// every re-simulation gets timed into a moving average expressed as a
// multiple of realtime. Drives the adaptive anchor spacing and the
// Loading estimates of the window engine (degrau 3); until then nothing
// feeds it in-game. Pure - host-tested in tests/test_rewind_tape.cpp.

struct RewindMeter
{
    float speedX;    // measured replay speed, multiples of realtime
    int   samples;

    void reset()
    {
        speedX = 0.0f;
        samples = 0;
    }

    // framesReplayed frames took wallTicks; one realtime frame costs
    // ticksPerFrame (svcGetSystemTick units on device)
    void note(uint32_t framesReplayed, uint64_t wallTicks, uint64_t ticksPerFrame)
    {
        if (framesReplayed == 0 || wallTicks == 0 || ticksPerFrame == 0) return;
        float x = (float)framesReplayed * (float)ticksPerFrame / (float)wallTicks;
        // EMA: heavy enough to smooth scene-to-scene swings, light enough
        // to track a game changing load within a few measurements
        speedX = (samples == 0) ? x : speedX + 0.25f * (x - speedX);
        samples++;
    }

    bool measured() const { return samples > 0; }
    float average() const { return speedX; }
};

#endif
