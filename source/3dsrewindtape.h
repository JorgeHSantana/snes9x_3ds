#ifndef _3DSREWINDTAPE_H
#define _3DSREWINDTAPE_H

#include <stdint.h>

// Input tape (rewind v2, docs/rewind-v2-spec.md section A.1): one packed
// pad word per emulated frame plus the MSU-1 status-port reads, whose
// values depend on SD timing (recording them closes the SD-stall
// divergence when a segment is re-simulated). The pad word is the value
// S9xReadJoypad returned - post-turbo, exactly what the core consumed.
//
// Sliding window over absolute frame numbers: the tape retains
// [baseFrame, baseFrame+count) and drops the oldest frame when full.
// MSU events tag the frame being executed (base+count); the pad push
// that follows commits that frame. validFrom marks how far back a
// replay can trust the tape: it advances when the window slides and
// when the MSU ring overflows (events lost = older frames unreplayable).
//
// Pure data structure over caller-owned buffers - no 3DS dependencies,
// host-tested in tests/test_rewind_tape.cpp.

struct RewindTape
{
    struct MsuRead
    {
        uint32_t frame;
        uint8_t  port;
        uint8_t  value;
    };

    uint32_t *pads;
    MsuRead  *msu;
    int       capacity;      // frames
    int       msuCapacity;

    uint32_t  baseFrame;
    uint32_t  count;
    int       msuStart;
    int       msuCount;
    uint32_t  validFrom;

    void init(uint32_t *padBuffer, int frameCapacity, MsuRead *msuBuffer, int msuEventCapacity)
    {
        pads = padBuffer;
        msu = msuBuffer;
        capacity = frameCapacity;
        msuCapacity = msuEventCapacity;
        clear(0);
    }

    bool valid() const { return pads != nullptr && capacity > 0; }

    void clear(uint32_t startFrame)
    {
        baseFrame = startFrame;
        count = 0;
        msuStart = 0;
        msuCount = 0;
        validFrom = startFrame;
    }

    // the frame currently being executed; its pad arrives with push()
    uint32_t nextFrame() const { return baseFrame + count; }

    void push(uint32_t pad)
    {
        if (count == (uint32_t)capacity) {
            baseFrame++;
            count--;
            if (validFrom < baseFrame)
                validFrom = baseFrame;
            while (msuCount > 0 && msu[msuStart].frame < baseFrame) {
                msuStart = (msuStart + 1) % msuCapacity;
                msuCount--;
            }
        }
        pads[(baseFrame + count) % (uint32_t)capacity] = pad;
        count++;
    }

    bool pad_at(uint32_t frame, uint32_t *out) const
    {
        if (frame < baseFrame || frame >= baseFrame + count) return false;
        *out = pads[frame % (uint32_t)capacity];
        return true;
    }

    void note_msu(uint8_t port, uint8_t value)
    {
        if (msuCapacity <= 0) return;
        if (msuCount == msuCapacity) {
            // losing the oldest event poisons every frame at or before it
            validFrom = msu[msuStart].frame + 1;
            msuStart = (msuStart + 1) % msuCapacity;
            msuCount--;
        }
        MsuRead &slot = msu[(msuStart + msuCount) % msuCapacity];
        slot.frame = nextFrame();
        slot.port = port;
        slot.value = value;
        msuCount++;
    }

    int msu_event_count() const { return msuCount; }

    // i = 0 is the oldest retained event
    const MsuRead &msu_event(int i) const
    {
        return msu[(msuStart + i) % msuCapacity];
    }

    // rollback: 'frame' becomes the present - drop everything after it
    void truncate_to(uint32_t frame)
    {
        if (frame + 1 <= baseFrame) {
            clear(frame + 1);
            return;
        }
        uint32_t keep = frame + 1 - baseFrame;
        if (keep < count)
            count = keep;
        while (msuCount > 0
                && msu[(msuStart + msuCount - 1) % msuCapacity].frame > frame)
            msuCount--;
        if (validFrom > frame + 1)
            validFrom = frame + 1;
    }
};

#endif
