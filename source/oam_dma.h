#ifndef OAM_DMA_H
#define OAM_DMA_H

// The frame's OAM upload is one DMA of 544 bytes from address 0 (Mario
// Kart does two a frame, mid-screen; MMX3 one in vblank). Through $2104 it
// costs two register writes per word with a flip state machine each; the
// word path in dma.cpp compares each source word against OAM and applies
// only the changed ones with the same update the register does.
//
// How many source bytes the word path may take: an even count inside the
// low table (words 0..255) starting on a word boundary, with priority
// rotation off (rotation moves FirstSprite on every address step and is
// left to the byte path). 0 means "byte path for everything".
static inline unsigned int oam_dma_word_bytes(unsigned int oamAddr, unsigned int oamFlip,
    unsigned int count, bool priorityRotation)
{
    if (priorityRotation || (oamFlip & 1) != 0 || oamAddr >= 0x100 || count < 2) return 0;
    const unsigned int room = (0x100 - oamAddr) * 2;
    const unsigned int even = count & ~1u;
    return even < room ? even : room;
}

#endif
