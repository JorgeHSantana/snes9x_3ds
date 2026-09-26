#ifndef LAYER_DEFER_H
#define LAYER_DEFER_H

// plain unsigned int: the core's uint32 is unsigned int on devkitARM, uint32_t is long there

// Per-layer render cursors (LayerRender.startY): the next scanline each
// layer still has to draw. A layer is "deferred" when its cursor is behind
// the line the emulation is at, which only a palette-only ($2122) flush can
// leave behind. Every other same-scanline flush must drain those layers
// before a register write changes what they would have drawn.
//
// Pure predicate shared by the drain and by the cached flag that lets
// FLUSH_REDRAW skip the drain call: an OAM DMA inside the visible frame
// flushes once per changed word (Mario Kart: ~270 calls per DMA, twice a
// frame, never finding anything to drain).
static inline bool layer_any_deferred(const unsigned int startY[5], unsigned int nextLine)
{
    for (int i = 0; i < 5; i++)
        if (startY[i] < nextLine) return true;
    return false;
}

#endif
