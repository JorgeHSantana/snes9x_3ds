#ifndef STALL_WATCH_H
#define STALL_WATCH_H

#include <stdint.h>

// A freeze that needs the power button leaves no log: the lines still in
// memory die with it, and a mark that waits for the SD writer thread never
// lands if that thread is part of the freeze. The watchdog (3dsstall.cpp)
// samples a counter the main thread bumps once a frame; this is its
// verdict, kept apart so the host tests cover it.

#define STALL_THRESHOLD_MS  2000   // the counter still for this long: a stall
#define STALL_FOLLOWUP_MS   1000   // one more report this much later: which threads still moved
#define STALL_TAIL_MAX      3000   // bytes of the stall file Send Log carries

struct StallWatch {
    uint32_t lastBeat;       // the counter at the last sample that saw it move
    uint64_t lastChangeMs;   // when that was
    uint8_t  reports;        // lines written for the stall in progress (0-2)
};

enum StallVerdict { STALL_NONE, STALL_BEGAN, STALL_STILL, STALL_ENDED };

// one sample. Disarmed (menus, sleep: nothing bumps the counter there)
// counts as moving, so a stall is only ever a frame that did not end.
static inline StallVerdict stall_watch_step(StallWatch* w, bool armed, uint32_t beat, uint64_t nowMs)
{
    if (!armed || beat != w->lastBeat) {
        const bool was = w->reports > 0;
        w->lastBeat = beat;
        w->lastChangeMs = nowMs;
        w->reports = 0;
        return was ? STALL_ENDED : STALL_NONE;
    }
    const uint64_t still = nowMs - w->lastChangeMs;
    if (w->reports == 0 && still >= STALL_THRESHOLD_MS) { w->reports = 1; return STALL_BEGAN; }
    if (w->reports == 1 && still >= STALL_THRESHOLD_MS + STALL_FOLLOWUP_MS) { w->reports = 2; return STALL_STILL; }
    return STALL_NONE;
}

#endif
