#ifndef APU_SLEEP_H
#define APU_SLEEP_H

#include <stdint.h>

// SPC700 sleep on a proven fixed point (perf-backlog E1a).
//
// Some sound drivers idle in a loop the wait-mark skip of apu.h cannot see:
// the Capcom one (Megaman X3) calls a routine that polls port 0, reads timer
// 0, adds it into a variable and branches back to the CALL - twelve
// instructions, 93% of the SPC700 opcodes of a frame, all of them leaving
// every register and every RAM byte as they were.
//
// The proof is by observation instead of by pattern. Every taken branch
// records the complete register state (the signature) and the cycle; every
// event that could make the next pass differ sets `dirty`: a write that
// changes a RAM byte or an output port, any other register write, a DSP
// read, a timer read that returns non-zero, a timer tick, a CPU write to
// $2140-$2143, a hit of the wait-mark skip. Coming back to the same
// signature with nothing dirty in between means the SPC700 is in the state
// it was in one period ago, so until the next timer tick or CPU port write
// it can only repeat that period. The caller then stops executing opcodes
// and, on the waking event, moves the cycle counter forward by whole periods
// (apu_sleep_forward) before resuming: the state after that is the one
// straight execution would have reached, cycle for cycle.

struct SApuSleepSig
{
    const uint8_t *pc;           // branch target
    const uint8_t *directPage;
    const uint8_t *wait1;        // the wait marks and counter of the old skip:
    const uint8_t *wait2;        // a pass that ends with other marks could
    uint32_t       waitCounter;  // take that skip on the next one
    uint16_t       ya;
    uint8_t        x, s;
    uint8_t        p;            // without the lazily packed C/Z/V/N bits
    uint8_t        carry, zero, overflow;
};

static inline bool apu_sleep_sig_equal(const SApuSleepSig *a, const SApuSleepSig *b)
{
    return a->pc == b->pc && a->ya == b->ya && a->x == b->x && a->s == b->s &&
           a->p == b->p && a->carry == b->carry && a->zero == b->zero &&
           a->overflow == b->overflow && a->directPage == b->directPage &&
           a->wait1 == b->wait1 && a->wait2 == b->wait2 && a->waitCounter == b->waitCounter;
}

struct SApuSleepDetector
{
    SApuSleepSig anchor;
    int32_t      anchorCycles;
    uint8_t      have;     // anchor is valid
    uint8_t      dirty;    // something changed since the last visit
    uint8_t      age;      // visits that did not match the anchor
};

// visits without a match before the anchor moves to the current one (the
// anchor may sit on a path the loop left)
static const int APU_SLEEP_REANCHOR = 16;

// One taken branch. Returns the period in cycles when this visit proves the
// fixed point, 0 otherwise. A dirty visit only clears the flag: the anchor
// is taken on the next clean one, so busy code pays three instructions here.
static inline int32_t apu_sleep_visit(SApuSleepDetector *d, const SApuSleepSig *sig, int32_t cycles)
{
    if (d->dirty)
    {
        d->dirty = 0;
        d->have = 0;
        return 0;
    }
    if (d->have)
    {
        if (apu_sleep_sig_equal(&d->anchor, sig))
        {
            int32_t period = cycles - d->anchorCycles;
            if (period > 0)
            {
                d->have = 0;
                return period;
            }
        }
        else if (++d->age < APU_SLEEP_REANCHOR)
            return 0;
    }
    d->anchor = *sig;
    d->anchorCycles = cycles;
    d->have = 1;
    d->age = 0;
    return 0;
}

// The cycle counter of a sleeper at `cycles`, moved by whole periods as far
// as it goes without passing `limit`: the last point at or before the limit
// where straight execution would have been in the anchor state. The opcode
// loop runs while the counter is <= its limit, so it continues from there
// exactly as it would have.
static inline int32_t apu_sleep_forward(int32_t cycles, int32_t limit, int32_t period)
{
    if (period <= 0 || limit < cycles)
        return cycles;
    return cycles + ((limit - cycles) / period) * period;
}

// Whether the next S9xIncrementAPUTimers will change a timer output
// ($FD-$FF), which is what the sleeper must be awake for. Mirrors that
// function: timer 2 steps every call, timers 0 and 1 every eighth.
static inline bool apu_sleep_timer_will_tick(int32_t counter, const uint8_t *enabled,
                                             const uint16_t *timer, const uint16_t *target)
{
    if (enabled[2] && timer[2] + 1 >= target[2])
        return true;
    if (counter + 1 != 8)
        return false;
    return (enabled[0] && timer[0] + 1 >= target[0]) ||
           (enabled[1] && timer[1] + 1 >= target[1]);
}

#endif
