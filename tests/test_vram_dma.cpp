#include "doctest.h"
#include "vram_dma.h"

static bool span(unsigned int mode, unsigned int vma, unsigned int count, unsigned int* off,
    bool toPpu = true, unsigned int b = 0x18, int inc = 1, unsigned int full = 0, unsigned int vinc = 1, bool high = true)
{
    return vram_dma_linear_span(toPpu, b, mode, inc, full, vinc, high, vma, count, off);
}

TEST_CASE("vram dma: the common word upload is a linear span at the doubled VMA address") {
    unsigned int off = 99;
    CHECK(span(1, 0x1000, 64, &off));
    CHECK(off == 0x2000);
    CHECK(span(5, 0x1000, 64, &off));
    CHECK(span(1, 0, 0x10000, &off));
    CHECK(off == 0);
}

TEST_CASE("vram dma: anything that is not a plain word upload keeps the flush") {
    unsigned int off = 0;
    CHECK_FALSE(span(0, 0x1000, 64, &off));                       // mode 0: $2118 only, half words
    CHECK_FALSE(span(1, 0x1000, 64, &off, false));                // PPU -> CPU
    CHECK_FALSE(span(1, 0x1000, 64, &off, true, 0x19));           // starts on the high byte
    CHECK_FALSE(span(1, 0x1000, 64, &off, true, 0x18, 0));        // fixed A address
    CHECK_FALSE(span(1, 0x1000, 64, &off, true, 0x18, -1));       // decrementing source
    CHECK_FALSE(span(1, 0x1000, 64, &off, true, 0x18, 1, 32));    // address remap
    CHECK_FALSE(span(1, 0x1000, 64, &off, true, 0x18, 1, 0, 32)); // word increment 32
    CHECK_FALSE(span(1, 0x1000, 64, &off, true, 0x18, 1, 0, 1, false)); // increment on low byte
    CHECK_FALSE(span(1, 0x1000, 0, &off));
}

TEST_CASE("vram dma: a span that wraps the end of VRAM is rejected") {
    unsigned int off = 0;
    CHECK(span(1, 0x7FE0, 64, &off));
    CHECK(off == 0xFFC0);
    CHECK_FALSE(span(1, 0x7FE0, 65, &off));
    CHECK(span(1, 0x8000, 2, &off));
    CHECK(off == 0);
}
