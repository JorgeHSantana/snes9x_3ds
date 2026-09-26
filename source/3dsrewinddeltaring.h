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
    int pendingDelta;            // delta slot reserved by commit_begin, -1 = none

    // A commit split in two so the delta encode (a memcmp/memcpy over the
    // whole state) can run off the emulation thread: commit_begin picks
    // the slots and evicts, the encode runs anywhere with the pointers it
    // returns, commit_end appends the entry. Between the two, push_ptr is
    // refused and both slots stay reserved.
    struct Pending
    {
        bool     valid;
        bool     tryDelta;       // encode allowed (discipline + slot found)
        uint32_t length, tag;
        int      pos;            // where the entry will land (thumbnails key off it)
        const uint8_t *kf; uint32_t kfLen; uint8_t kfSlot;
        const uint8_t *state;
        uint8_t *out; uint32_t outCapacity;
    };

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
        pendingDelta = -1;
    }

    bool commit_pending() const { return pendingDelta >= 0 || (stagingKf >= 0 && pendingStaging); }
    bool pendingStaging = false;

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
        // the staging slot is reserved even before its entry exists, and so
        // is the delta slot of a commit in flight
        if (kind == KIND_KEYFRAME && slot == stagingKf) return true;
        return kind == KIND_DELTA && slot == pendingDelta;
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
        if (!valid() || commit_pending()) return nullptr;
        int s = freeSlot(KIND_KEYFRAME, kfSlots);
        while (s < 0 && count > 0) {
            dropOldestGroup();
            s = freeSlot(KIND_KEYFRAME, kfSlots);
        }
        if (s < 0) return nullptr;
        stagingKf = s;
        return kfPool + (size_t)s * slotSize;
    }

    // step 1 (emulation thread): decide delta vs keyframe, reserve the
    // slots, evict so the entry has a place, and hand out the pointers
    bool commit_begin(uint32_t length, uint32_t tag, Pending *pc)
    {
        if (pc) pc->valid = false;
        if (!valid() || stagingKf < 0 || pc == nullptr || commit_pending()) return false;

        // an entry slot for the coming append, decided now so the entry's
        // position is known before commit_end (thumbnails are stored by it)
        while (count >= entryCapacity)
            dropOldestGroup();

        pc->valid = true; pc->tryDelta = false;
        pc->length = length; pc->tag = tag;
        pc->state = kfPool + (size_t)stagingKf * slotSize;
        pc->kf = nullptr; pc->kfLen = 0; pc->kfSlot = 0;
        pc->out = nullptr; pc->outCapacity = 0;

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
                pc->tryDelta = true;
                pc->kf = kfPool + (size_t)kfe.slot * slotSize;
                pc->kfLen = kfe.len; pc->kfSlot = kfe.slot;
                pc->out = deltaPool + (size_t)dslot * deltaSlotSize;
                pc->outCapacity = deltaSlotSize;
                pendingDelta = dslot;
            }
        }
        pc->pos = (start + count) % entryCapacity;
        pendingStaging = true;
        return true;
    }

    // the encode itself (pure; the worker runs this): 0 = keyframe fallback
    static uint32_t commit_encode(const Pending &pc, uint32_t pageSize)
    {
        if (!pc.valid || !pc.tryDelta) return 0;
        return RewindDelta::encode(pc.kf, pc.kfLen, pc.state, pc.length,
                                   pageSize, pc.out, pc.outCapacity);
    }

    // step 2 (emulation thread): append the entry the encode produced
    void commit_end(const Pending &pc, uint32_t encoded)
    {
        if (!valid() || !pc.valid || stagingKf < 0) return;
        Entry e = {};
        if (pc.tryDelta && encoded > 0 && pendingDelta >= 0) {
            e.kind = KIND_DELTA; e.slot = (uint8_t)pendingDelta;
            e.kfSlot = pc.kfSlot; e.len = encoded;
            e.kfLen = pc.kfLen; e.tag = pc.tag;
            pendingDelta = -1; stagingKf = -1; pendingStaging = false;
            append(e);
            sinceKeyframe++;
            return;
        }
        // keyframe commit: the staged state stays where it is
        e.kind = KIND_KEYFRAME; e.slot = (uint8_t)stagingKf;
        e.len = pc.length; e.tag = pc.tag;
        pendingDelta = -1; stagingKf = -1; pendingStaging = false;
        append(e);
        sinceKeyframe = 1;
    }

    // the synchronous form (tests, and any caller without a worker)
    void push_commit(uint32_t length, uint32_t tag)
    {
        Pending pc;
        if (!commit_begin(length, tag, &pc)) return;
        commit_end(pc, commit_encode(pc, pageSize));
    }

    // stable position of the entry in entries[] - callers key per-entry
    // side data (thumbnails) off it, so evictions recycle positions
    int entry_pos(int back) const
    {
        return (start + count - 1 - back) % entryCapacity;
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

    // live rewind: consume the newest entry after restoring it
    void pop_newest()
    {
        if (count == 0) return;
        count--;
        sinceKeyframe = keyframeInterval;   // next capture starts a fresh keyframe
    }

    // Max History ceiling: drop whole oldest groups past maxEntries -
    // but never the newest group (a ceiling smaller than one group would
    // otherwise empty the ring entirely; host test caught it)
    void trim_to(int maxEntries)
    {
        if (maxEntries < 1) maxEntries = 1;
        while (count > maxEntries) {
            int kfCount = 0;
            for (int i = 0; i < count; i++)
                if (fifoAt(i).kind == KIND_KEYFRAME) kfCount++;
            if (kfCount <= 1) break;
            dropOldestGroup();
        }
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
