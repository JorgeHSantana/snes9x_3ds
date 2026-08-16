#ifndef _3DSREWINDTICKS_H
#define _3DSREWINDTICKS_H

#include <stdint.h>

// Window tick-states (rewind v2 degrau 3, docs/rewind-v2-spec.md B): a
// small pool of full savestates at 1s ticks around the timeline cursor.
// The pool only does bookkeeping - which absolute frame occupies which
// slot, what to evict, what to fill next; the states themselves live in
// caller-owned slot buffers and the caller runs the replays that fill
// them. Eviction is farthest-from-cursor, so the pool naturally holds
// the n-1/n/n+1 neighbourhood the spec asks for. Host-tested.

struct RewindTicks
{
    static constexpr uint32_t NO_FRAME = 0;   // frame numbers start at 1

    uint32_t *frames;     // per slot: absolute frame of the state, 0 = free
    int       capacity;

    void init(uint32_t *frameBuf, int slotCount)
    {
        frames = frameBuf;
        capacity = slotCount;
        clear();
    }

    bool valid() const { return frames != nullptr && capacity > 0; }

    void clear()
    {
        for (int i = 0; i < capacity; i++)
            frames[i] = NO_FRAME;
    }

    int find(uint32_t frame) const
    {
        for (int i = 0; i < capacity; i++)
            if (frames[i] == frame) return i;
        return -1;
    }

    // slot to store 'frame' into: a free slot, or the resident state
    // farthest from the cursor (never evicts something closer than the
    // newcomer). Returns -1 when the newcomer is the worst candidate.
    int alloc(uint32_t frame, uint32_t cursorFrame)
    {
        int slot = -1;
        uint32_t worstDist = dist(frame, cursorFrame);
        for (int i = 0; i < capacity; i++) {
            if (frames[i] == NO_FRAME) { slot = i; break; }
            uint32_t d = dist(frames[i], cursorFrame);
            if (d > worstDist) { worstDist = d; slot = i; }
        }
        if (slot >= 0)
            frames[slot] = frame;
        return slot;
    }

    // drop states in the abandoned future after a rollback
    void drop_after(uint32_t frame)
    {
        for (int i = 0; i < capacity; i++)
            if (frames[i] != NO_FRAME && frames[i] > frame)
                frames[i] = NO_FRAME;
    }

    // the resident state closest below-or-at 'frame' - the cheapest
    // replay source for materializing it (the caller compares against
    // its anchors too). Returns slot index or -1.
    int best_source(uint32_t frame) const
    {
        int slot = -1;
        uint32_t best = 0;
        for (int i = 0; i < capacity; i++) {
            if (frames[i] == NO_FRAME || frames[i] > frame) continue;
            if (slot < 0 || frames[i] > best) { best = frames[i]; slot = i; }
        }
        return slot;
    }

    // prefetch planning: the missing tick nearest to the cursor within
    // radius, alternating outward (cursor first, then +-1, +-2...).
    // tickFrames = frames between ticks; frames are clamped to
    // [lowest, highest]. Returns NO_FRAME when the neighbourhood is full.
    uint32_t next_missing(uint32_t cursorFrame, uint32_t tickFrames, int radius,
                          uint32_t lowest, uint32_t highest) const
    {
        for (int r = 0; r <= radius; r++) {
            for (int sign = 0; sign < (r == 0 ? 1 : 2); sign++) {
                int64_t f = (int64_t)cursorFrame
                    + (sign == 0 ? -(int64_t)r : (int64_t)r) * tickFrames;
                if (f < (int64_t)lowest || f > (int64_t)highest) continue;
                if (find((uint32_t)f) < 0) return (uint32_t)f;
            }
        }
        return NO_FRAME;
    }

private:
    static uint32_t dist(uint32_t a, uint32_t b)
    {
        return a > b ? a - b : b - a;
    }
};

#endif
