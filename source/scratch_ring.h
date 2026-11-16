#ifndef SCRATCH_RING_H
#define SCRATCH_RING_H

#include <string.h>

// A ring of small fixed-size vertex entries that the GPU reads later, when
// the command list runs (at a frame split or at the frame's end).
//
// Two rules:
//
// - An entry whose bytes are already in the ring is reused and nothing is
//   written. Memory that did not change needs no cache flush before the
//   next submission, and a flush is a kernel call of about 57 us in Old
//   3DS mode whatever the size (console log of 2026-11-15). The glow's
//   clear rect and composite quads are the same from one frame to the
//   next while the settings and the slider stand still, and they were
//   written, and flushed, at each of the frame's five submissions.
//
// - An entry is written over only when no draw of this frame or of the one
//   before used it: a command list still to run may read it (issue #77,
//   the clear rect that shared a slot). With every entry in use the write
//   goes to the cursor's entry, the oldest written.
//
// The search looks at the SCRATCH_RING_WINDOW entries written last, so a
// frame whose entries all changed (the slider moving) pays a bounded
// number of compares. An entry outside the window is written again.

#define SCRATCH_RING_ENTRIES 64
#define SCRATCH_RING_WINDOW  32

struct ScratchRing
{
    unsigned int  lastUse[SCRATCH_RING_ENTRIES];   // frame of the last draw that used the entry
    unsigned char valid[SCRATCH_RING_ENTRIES];     // written by this ring (not what the allocator left)
    int           cursor;                          // the next entry to write
};

static inline void scratch_ring_reset(ScratchRing *r)
{
    memset(r, 0, sizeof(*r));
}

// The entry of the ring at `base` (entries of `entryBytes` bytes) that holds
// `entry` for a draw of frame `frame`. *wrote tells whether memory changed.
static inline int scratch_ring_put(ScratchRing *r, void *base, unsigned int entryBytes,
                                   const void *entry, unsigned int frame, bool *wrote)
{
    unsigned char *b = (unsigned char *)base;
    for (int k = 1; k <= SCRATCH_RING_WINDOW; k++) {
        const int i = (r->cursor - k + SCRATCH_RING_ENTRIES) % SCRATCH_RING_ENTRIES;
        if (r->valid[i] && memcmp(b + (unsigned int)i * entryBytes, entry, entryBytes) == 0) {
            r->lastUse[i] = frame;
            *wrote = false;
            return i;
        }
    }
    int at = r->cursor;
    for (int k = 0; k < SCRATCH_RING_ENTRIES; k++) {
        const int i = (r->cursor + k) % SCRATCH_RING_ENTRIES;
        if (!r->valid[i] || frame - r->lastUse[i] >= 2u) { at = i; break; }
    }
    memcpy(b + (unsigned int)at * entryBytes, entry, entryBytes);
    r->valid[at] = 1;
    r->lastUse[at] = frame;
    r->cursor = (at + 1) % SCRATCH_RING_ENTRIES;
    *wrote = true;
    return at;
}

#endif
