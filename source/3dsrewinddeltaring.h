#ifndef _3DSREWINDDELTARING_H
#define _3DSREWINDDELTARING_H

#include <stdint.h>
#include <string.h>

#include "3dsrewinddelta.h"

// Keyframe-disciplined snapshot ring (issue #37, step 2): captures are
// stored either as full keyframes (512KB slots) or as page deltas against
// the newest keyframe (small slots). The staging trick keeps pushes
// zero-copy: the caller freezes into the NEXT free keyframe slot via
// push_ptr(); push_commit() then either keeps it there (keyframe) or
// encodes it into a delta slot and leaves the staging slot free.
//
// Eviction is by GROUP - the oldest keyframe and every delta that depends
// on it fall together, so a live delta's keyframe can never die first.
// FIFO of entries, LIFO reads by 'back' (0 = newest), same contract as
// RewindRing. Pure bookkeeping over caller buffers, host-tested.

struct RewindDeltaRing
{
    enum : uint8_t { KIND_KEYFRAME = 0, KIND_DELTA = 1 };

    struct Entry
    {
        uint8_t  kind;
        uint8_t  slot;      // index into its kind's pool
        uint8_t  kfSlot;    // deltas: the keyframe slot they decode against
        uint32_t len;       // stored bytes (state len for KF, delta len for delta)
        uint32_t kfLen;     // deltas: their keyframe's state length
        uint32_t tag;
    };

    uint8_t *kfPool;    int kfSlots;    uint32_t slotSize;
    uint8_t *deltaPool; int deltaSlots; uint32_t deltaSlotSize;
    Entry   *entries;   int entryCapacity;
    uint32_t pageSize;
    int      keyframeInterval;   // a keyframe at least every K captures

    int start, count;            // FIFO window over entries[]
    int sinceKeyframe;
    int stagingKf;               // slot handed out by push_ptr, -1 = none

    void init(uint8_t *kfBuf, int kfCount, uint32_t stateSlotSize,
              uint8_t *deltaBuf, int deltaCount, uint32_t deltaSlotBytes,
              Entry *entryBuf, int entryCount,
              uint32_t deltaPageSize, int kfInterval)
    {
        kfPool = kfBuf; kfSlots = kfCount; slotSize = stateSlotSize;
        deltaPool = deltaBuf; deltaSlots = deltaCount; deltaSlotSize = deltaSlotBytes;
        entries = entryBuf; entryCapacity = entryCount;
        pageSize = deltaPageSize; keyframeInterval = kfInterval;
        clear();
    }

    bool valid() const { return kfPool != nullptr && entries != nullptr && kfSlots > 0; }

    void clear()
    {
        start = 0; count = 0;
        sinceKeyframe = 0;
        stagingKf = -1;
    }

    const Entry &at(int back) const
    {
        return entries[(start + count - 1 - back) % entryCapacity];
    }

private:
    Entry &fifoAt(int i) { return entries[(start + i) % entryCapacity]; }
    const Entry &fifoAt(int i) const { return entries[(start + i) % entryCapacity]; }

    bool slotLive(uint8_t kind, int slot) const
    {
        for (int i = 0; i < count; i++)
            if (fifoAt(i).kind == kind && fifoAt(i).slot == slot) return true;
        // the staging slot is reserved even before its entry exists
        return kind == KIND_KEYFRAME && slot == stagingKf;
    }

    int freeSlot(uint8_t kind, int slotCount) const
    {
        for (int s = 0; s < slotCount; s++)
            if (!slotLive(kind, s)) return s;
        return -1;
    }

    // drop the oldest keyframe and every entry up to (not including) the
    // next keyframe - the whole dependent group leaves together
    void dropOldestGroup()
    {
        if (count == 0) return;
        do {
            start = (start + 1) % entryCapacity;
            count--;
        } while (count > 0 && fifoAt(0).kind != KIND_KEYFRAME);
    }

    // the newest keyframe entry (deltas encode against it), -1 if none
    int newestKeyframe() const
    {
        for (int back = 0; back < count; back++)
            if (at(back).kind == KIND_KEYFRAME) return back;
        return -1;
    }

    void append(const Entry &e)
    {
        while (count >= entryCapacity)
            dropOldestGroup();
        entries[(start + count) % entryCapacity] = e;
        count++;
    }

public:
    // staging buffer for the caller's full-state freeze; evicts old
    // groups until a keyframe slot frees up
    uint8_t *push_ptr()
    {
        if (!valid()) return nullptr;
        int s = freeSlot(KIND_KEYFRAME, kfSlots);
        while (s < 0 && count > 0) {
            dropOldestGroup();
            s = freeSlot(KIND_KEYFRAME, kfSlots);
        }
        if (s < 0) return nullptr;
        stagingKf = s;
        return kfPool + (size_t)s * slotSize;
    }

    void push_commit(uint32_t length, uint32_t tag)
    {
        if (!valid() || stagingKf < 0) return;

        // try a delta while the discipline allows it and a keyframe exists
        int kfBack = newestKeyframe();
        if (kfBack >= 0 && sinceKeyframe < keyframeInterval && deltaSlots > 0) {
            int kfCount = 0;
            for (int i = 0; i < count; i++)
                if (fifoAt(i).kind == KIND_KEYFRAME) kfCount++;

            int dslot = freeSlot(KIND_DELTA, deltaSlots);
            // evict whole old groups for a delta slot - but never the
            // newest group, which this delta is about to join
            while (dslot < 0 && kfCount > 1) {
                dropOldestGroup();
                kfCount--;
                dslot = freeSlot(KIND_DELTA, deltaSlots);
            }
            kfBack = newestKeyframe();
            if (kfBack >= 0 && dslot >= 0) {
                const Entry &kfe = at(kfBack);
                uint32_t encoded = RewindDelta::encode(
                    kfPool + (size_t)kfe.slot * slotSize, kfe.len,
                    kfPool + (size_t)stagingKf * slotSize, length,
                    pageSize,
                    deltaPool + (size_t)dslot * deltaSlotSize, deltaSlotSize);
                if (encoded > 0) {
                    Entry e = {};
                    e.kind = KIND_DELTA; e.slot = (uint8_t)dslot;
                    e.kfSlot = kfe.slot; e.len = encoded;
                    e.kfLen = kfe.len; e.tag = tag;
                    stagingKf = -1;
                    append(e);
                    sinceKeyframe++;
                    return;
                }
            }
        }

        // keyframe commit: the staged state stays where it is
        Entry e = {};
        e.kind = KIND_KEYFRAME; e.slot = (uint8_t)stagingKf;
        e.len = length; e.tag = tag;
        stagingKf = -1;
        append(e);
        sinceKeyframe = 1;
    }

    bool tag_at(int back, uint32_t *tag) const
    {
        if (!valid() || back < 0 || back >= count) return false;
        *tag = at(back).tag;
        return true;
    }

    // reconstructs the state at 'back' into out; returns the state length
    uint32_t read_at(int back, uint8_t *out, uint32_t outCapacity) const
    {
        if (!valid() || back < 0 || back >= count || out == nullptr) return 0;
        const Entry &e = at(back);
        if (e.kind == KIND_KEYFRAME) {
            if (outCapacity < e.len) return 0;
            memcpy(out, kfPool + (size_t)e.slot * slotSize, e.len);
            return e.len;
        }
        return RewindDelta::decode(
            kfPool + (size_t)e.kfSlot * slotSize, e.kfLen,
            deltaPool + (size_t)e.slot * deltaSlotSize, e.len,
            pageSize, out, outCapacity);
    }

    // keep 'back' as the newest entry, drop everything newer
    void rollback_to(int back)
    {
        if (!valid() || back < 0 || back >= count) return;
        count -= back;
        sinceKeyframe = keyframeInterval;   // next capture starts a fresh keyframe
    }
};

#endif
