#include "doctest.h"
#include "msu1.h"

TEST_CASE("DMA B-bus offset table, modes 0-7 over 8 indexes")
{
    // SNES DMA write patterns (snes.nesdev.org/wiki/DMA):
    // 0: B          1: B,B+1      2: B,B        3: B,B,B+1,B+1
    // 4: B..B+3     5=1, 6=2, 7=3 (mirrors)
    static const uint8_t expected[8][8] = {
        {0,0,0,0,0,0,0,0},
        {0,1,0,1,0,1,0,1},
        {0,0,0,0,0,0,0,0},
        {0,0,1,1,0,0,1,1},
        {0,1,2,3,0,1,2,3},
        {0,1,0,1,0,1,0,1},
        {0,0,0,0,0,0,0,0},
        {0,0,1,1,0,0,1,1},
    };
    for (int mode = 0; mode < 8; mode++) {
        for (uint32_t i = 0; i < 8; i++) {
            CAPTURE(mode); CAPTURE(i);
            CHECK(msu1_dma_b_offset((uint8_t)mode, i) == expected[mode][i]);
        }
    }
}
