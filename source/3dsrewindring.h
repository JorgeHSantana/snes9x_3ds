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
    uint32_t *tags;      // caller-defined stamp per slot (e.g. frame number)
    int       slots;
    uint32_t  slotSize;

    int head;    // next slot to write
    int count;   // valid snapshots stored

    void init(uint8_t *poolBuf, uint32_t *lenBuf, uint32_t *tagBuf,
              int slotCount, uint32_t size) {
        pool = poolBuf;
        lengths = lenBuf;
        tags = tagBuf;
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
    uint8_t *push_ptr() const {
        return pool + (size_t)head * slotSize;
    }

    // slot index of the entry 'back' steps behind the newest (0 = newest);
    // only valid when back < count
    int slot_at(int back) const {
        return (head - 1 - back + 2 * slots) % slots;
    }

    // commit the snapshot serialized at push_ptr(); overwrites the oldest
    // entry once the ring is full
    void push_commit(uint32_t length, uint32_t tag) {
        lengths[head] = length;
        tags[head] = tag;
        head = (head + 1) % slots;
        if (count < slots) count++;
    }

    // newest snapshot (the one a rewind step restores); false when empty
    bool pop_peek(const uint8_t **data, uint32_t *length) const {
        if (count == 0) return false;
        int idx = (head - 1 + slots) % slots;
        *data = pool + (size_t)idx * slotSize;
        *length = lengths[idx];
        return true;
    }

    // consume the snapshot returned by pop_peek
    void pop_commit() {
        if (count == 0) return;
        head = (head - 1 + slots) % slots;
        count--;
    }

    // snapshot 'back' steps behind the newest (0 = newest); false when
    // out of range
    bool peek_at(int back, const uint8_t **data, uint32_t *length,
                 uint32_t *tag) const {
        if (back < 0 || back >= count) return false;
        int idx = slot_at(back);
        *data = pool + (size_t)idx * slotSize;
        *length = lengths[idx];
        *tag = tags[idx];
        return true;
    }

    // resume from the snapshot 'back' steps behind the newest: entries
    // newer than it are dropped (abandoned future branch), the chosen one
    // stays as the newest so it can be rewound to again
    void rollback_to(int back) {
        if (back < 0 || back >= count) return;
        head = (slot_at(back) + 1) % slots;
        count -= back;
    }
};

#endif
