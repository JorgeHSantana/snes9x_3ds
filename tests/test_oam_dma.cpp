#include "doctest.h"
#include "oam_dma.h"

TEST_CASE("oam dma: the standard full upload takes the whole low table by words") {
    CHECK(oam_dma_word_bytes(0, 0, 544, false) == 512);
    CHECK(oam_dma_word_bytes(0, 0, 512, false) == 512);
    CHECK(oam_dma_word_bytes(0, 0, 100, false) == 100);
    CHECK(oam_dma_word_bytes(0x80, 0, 544, false) == 256);
}

TEST_CASE("oam dma: odd counts leave the trailing byte to the byte path") {
    CHECK(oam_dma_word_bytes(0, 0, 101, false) == 100);
    CHECK(oam_dma_word_bytes(0, 0, 1, false) == 0);
}

TEST_CASE("oam dma: rotation, a pending low byte, or the high table use the byte path") {
    CHECK(oam_dma_word_bytes(0, 0, 544, true) == 0);
    CHECK(oam_dma_word_bytes(0, 1, 544, false) == 0);
    CHECK(oam_dma_word_bytes(0x100, 0, 32, false) == 0);
    CHECK(oam_dma_word_bytes(0x1FF, 0, 32, false) == 0);
}
