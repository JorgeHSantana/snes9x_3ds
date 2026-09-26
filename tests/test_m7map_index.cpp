#include "doctest.h"
#include "m7map_index.h"
#include <string.h>
#include <stdlib.h>

namespace {
Mode7MapIndex idx;   // 64 KiB: static, not on the test stack
uint8_t vram[0x10000];

int positionsOf(unsigned c) { int n = 0; idx.for_each_position(c, [&](int) { n++; }); return n; }
bool consistent(const uint8_t* v) {
    for (unsigned c = 0; c < 256; c++) {
        int n = 0; bool ok = true;
        idx.for_each_position(c, [&](int p) { n++; if (v[p * 2] != c) ok = false; });
        if (!ok || n != idx.count[c]) return false;
    }
    int total = 0; for (unsigned c = 0; c < 256; c++) total += idx.count[c];
    return total == 16384;
}
}

TEST_CASE("m7map index: rebuild lists every position under its char, ascending") {
    memset(vram, 0, sizeof(vram));
    for (int p = 0; p < 16384; p++) vram[p * 2] = (uint8_t)(p % 7);
    idx.rebuild(vram);
    CHECK(idx.valid);
    CHECK(consistent(vram));
    CHECK(idx.used(3));
    CHECK_FALSE(idx.used(7));
    int last = -1; bool ascending = true;
    idx.for_each_position(2, [&](int p) { if (p <= last) ascending = false; last = p; });
    CHECK(ascending);
    CHECK(positionsOf(0) == 2341);
}

TEST_CASE("m7map index: one char on the whole map, then a relink moves one position") {
    memset(vram, 0, sizeof(vram));
    idx.rebuild(vram);
    CHECK(idx.count[0] == 16384);
    vram[100 * 2] = 9; idx.relink(100, 0, 9);
    CHECK(idx.count[0] == 16383);
    CHECK(idx.count[9] == 1);
    CHECK(consistent(vram));
    vram[100 * 2] = 0; idx.relink(100, 9, 0);
    CHECK_FALSE(idx.used(9));
    CHECK(consistent(vram));
}

TEST_CASE("m7map index: random relinks keep the lists equal to a fresh rebuild") {
    srand(3);
    for (int p = 0; p < 16384; p++) vram[p * 2] = (uint8_t)(rand() & 0xFF);
    idx.rebuild(vram);
    for (int i = 0; i < 20000; i++) {
        int p = rand() % 16384; uint8_t c = (uint8_t)(rand() & 0xFF);
        uint8_t old = vram[p * 2]; vram[p * 2] = c;
        idx.relink(p, old, c);
    }
    CHECK(consistent(vram));
    static Mode7MapIndex fresh; fresh.rebuild(vram);
    for (unsigned c = 0; c < 256; c++) CHECK(fresh.count[c] == idx.count[c]);
}

TEST_CASE("m7map index: relinks are ignored while invalid, and a rebuild restores everything") {
    idx.invalidate();
    CHECK_FALSE(idx.valid);
    idx.relink(5, 1, 2);
    idx.rebuild(vram);
    CHECK(consistent(vram));
}
