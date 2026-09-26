#ifndef VRAM_DMA_H
#define VRAM_DMA_H

// A DMA into VRAM forces a section render before it runs, so the scanlines
// above the transfer are drawn with the old tiles (Mickey & Donald 3 breaks
// without it). Mario Kart makes ~2.3 such transfers per frame in the visible
// area and 42% of them rewrite bytes VRAM already holds; each of those still
// split the frame into one more section (~130 us of render per section on
// the Old 3DS scale). When the transfer is a plain linear byte span and its
// source equals the destination, nothing observable changes (the write
// macros only invalidate on a differing byte), so the flush is skipped.
//
// This predicate says whether a transfer is that plain span and where it
// lands. Everything it rejects keeps the unconditional flush.
//   mode 1 or 5 (alternating $2118/$2119 = one word per pair), B = $2118,
//   A-bus +1, no address remap (FullGraphicCount 0), word increment 1,
//   increment on the high byte, and the span does not wrap VRAM.
static inline bool vram_dma_linear_span(bool toPpu, unsigned int bAddress, unsigned int transferMode,
    int aInc, unsigned int fullGraphicCount, unsigned int vmaIncrement, bool vmaHigh,
    unsigned int vmaAddress, unsigned int count, unsigned int* vramOffset)
{
    if (!toPpu || bAddress != 0x18) return false;
    if (transferMode != 1 && transferMode != 5) return false;
    if (aInc != 1 || fullGraphicCount != 0 || vmaIncrement != 1 || !vmaHigh) return false;
    if (count == 0) return false;
    const unsigned int off = (vmaAddress << 1) & 0xFFFF;
    if (off + count > 0x10000) return false;
    *vramOffset = off;
    return true;
}

#endif
