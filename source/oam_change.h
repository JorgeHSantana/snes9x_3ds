#ifndef OAM_CHANGE_H
#define OAM_CHANGE_H

#include <stdint.h>

// Only low-OAM word writes use this predicate. Appearance is consumed live
// by the renderer; scanline lists cache position, size and vertically flipped
// tile rows. High OAM, OBSEL and priority-rotation invalidation stay separate.
static inline bool oam_word_changes_geometry(uint16_t address, uint8_t old_low,
    uint8_t old_high, uint8_t new_low, uint8_t new_high)
{
    if (address >= 512 || (address & 1) != 0) return true;
    if ((address & 2) != 0) return ((old_high ^ new_high) & 0x80) != 0;
    return old_low != new_low || old_high != new_high;
}

#endif
