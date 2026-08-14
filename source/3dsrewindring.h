#ifndef _3DSREWINDRING_H
#define _3DSREWINDRING_H

// Pure ring-buffer bookkeeping for the rewind feature (issue #12), shared
// with the host test suite. The ring owns no memory: the caller hands it a
// pool of `slots` fixed-size slots plus a parallel length array. Pushing
// overwrites the oldest snapshot once full; popping consumes the newest
// first (LIFO), which is exactly the rewind direction.

#include <stdint.h>
#include <stddef.h>

struct RewindRing {
    uint8_t  *pool;      // slots * slotSize bytes
    uint32_t *lengths;   // one entry per slot
    int       slots;
    uint32_t  slotSize;

    int head;    // next slot to write
    int count;   // valid snapshots stored

    void init(uint8_t *poolBuf, uint32_t *lenBuf, int slotCount, uint32_t size) {
        pool = poolBuf;
        lengths = lenBuf;
        slots = slotCount;
        slotSize = size;
        clear();
    }

    void clear() {
        head = 0;
        count = 0;
    }

    bool valid() const { return pool != NULL && slots > 0; }
    bool empty() const { return count == 0; }
    bool full()  const { return count == slots; }

    // where the next snapshot should be serialized to
    uint8_t *pushPtr() const {
        return pool + (size_t)head * slotSize;
    }

    // commit the snapshot serialized at pushPtr(); overwrites the oldest
    // entry once the ring is full
    void pushCommit(uint32_t length) {
        lengths[head] = length;
        head = (head + 1) % slots;
        if (count < slots) count++;
    }

    // newest snapshot (the one a rewind step restores); false when empty
    bool popPeek(const uint8_t **data, uint32_t *length) const {
        if (count == 0) return false;
        int idx = (head - 1 + slots) % slots;
        *data = pool + (size_t)idx * slotSize;
        *length = lengths[idx];
        return true;
    }

    // consume the snapshot returned by popPeek
    void popCommit() {
        if (count == 0) return;
        head = (head - 1 + slots) % slots;
        count--;
    }
};

#endif
