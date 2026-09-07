#include "doctest.h"
#include "../source/3dsgroundsprites.h"

TEST_CASE("ground: row encoding clamps the plane w into a byte, 255 = nearest") {
    CHECK(groundRowEncode(256) == 255);
    CHECK(groundRowEncode(255) == 255);
    CHECK(groundRowEncode(128) == 128);
    CHECK(groundRowEncode(8) == 8);
    CHECK(groundRowEncode(0) == 1);      // a real plane row never reads as "not on plane"
    CHECK(groundRowEncode(-5) == 1);
}

TEST_CASE("ground: rows outside the plane read 0; past the bottom stands on the last row") {
    GroundFrame f; groundFrameReset(&f);
    groundRowSet(&f, 100, 64);
    groundRowSet(&f, 239, 256);
    CHECK(groundRowAt(&f, 100) == 64);
    CHECK(groundRowAt(&f, 99) == 0);
    CHECK(groundRowAt(&f, -1) == 0);
    CHECK(groundRowAt(&f, 260) == 255);  // clamped to row 239
    CHECK(groundRowsPresent(&f));
    groundFrameStart(&f);
    CHECK_FALSE(groundRowsPresent(&f));
    groundRowSet(&f, 300, 10);           // out of range: ignored
    CHECK_FALSE(groundRowsPresent(&f));
}

TEST_CASE("ground: signatures are the tile sheet row + palette, stable across an animation") {
    uint32_t a = groundSigMake(0x1A0, 3);
    CHECK(groundSigName(a) == 0x1A0);
    CHECK(groundSigPalette(a) == 3);
    CHECK(groundSigMake(0x1A0, 3) == a);
    CHECK(groundSigMake(0x1A0, 4) != a);
    CHECK(groundSigMake(0x1A4, 3) == a);   // same row of the sheet (animation frame)
    CHECK(groundSigMake(0x1B0, 3) != a);   // next row
    CHECK(groundSigMake(0x3A0, 3) == a);   // 9-bit name
}

TEST_CASE("ground: the slot table registers on first sight, reuses, and overflows to 0") {
    GroundFrame f; groundFrameReset(&f);
    int s1 = groundSlotFor(&f, groundSigMake(1 << 4, 0), 10, 20);
    int s2 = groundSlotFor(&f, groundSigMake(2 << 4, 0), 30, 40);
    CHECK(s1 == 1);
    CHECK(s2 == 2);
    CHECK(groundSlotFor(&f, groundSigMake(1 << 4, 0), 99, 99) == 1);   // same graphic, same slot
    CHECK(f.sigX[1] == 10);                                        // first position kept
    CHECK(f.sigY[1] == 20);
    for (int i = 3; i < GROUND_SLOTS; i++)
        CHECK(groundSlotFor(&f, groundSigMake(i << 4, 0), 0, 0) == i);
    CHECK(f.sigCount == GROUND_SLOTS);
    CHECK(groundSlotFor(&f, groundSigMake(0x100, 5), 0, 0) == 0);  // full
    CHECK(groundSlotFor(&f, groundSigMake(5 << 4, 0), 0, 0) == 5); // known ones still resolve
    groundFrameStart(&f);
    CHECK(f.sigCount == 1);
}

TEST_CASE("ground: the vertex w packs row and slot and unpacks exactly") {
    int rw, sl;
    groundVertexDecode(groundVertexW(255, 1), &rw, &sl);
    CHECK(rw == 255); CHECK(sl == 1);
    groundVertexDecode(groundVertexW(0, 31), &rw, &sl);
    CHECK(rw == 0); CHECK(sl == 31);
    groundVertexDecode(groundVertexW(17, 0), &rw, &sl);
    CHECK(rw == 17); CHECK(sl == 0);
    CHECK(groundVertexW(255, 31) == 255 + 256 * 31);   // fits an int16
}

TEST_CASE("ground: row shift runs from the near gauge to the far gauge, monotonic") {
    CHECK(groundRowShift(255, 2.0f, -6.0f) == doctest::Approx(2.0f));
    CHECK(groundRowShift(0, 2.0f, -6.0f) == doctest::Approx(-6.0f));
    CHECK(groundRowShift(255, -4.0f, -4.0f) == doctest::Approx(-4.0f));
    float prev = groundRowShift(255, 2.0f, -6.0f);
    for (int w = 254; w >= 1; w--) {
        float cur = groundRowShift(w, 2.0f, -6.0f);
        CHECK(cur <= prev);
        prev = cur;
    }
}

TEST_CASE("ground: exceptions toggle in and out, capped") {
    uint32_t list[GROUND_EXCEPTIONS_MAX];
    int n = 0;
    uint32_t lakitu = groundSigMake(0x40, 5);
    n = groundToggleException(list, n, lakitu);
    CHECK(n == 1);
    CHECK(groundIsException(list, n, lakitu));
    CHECK_FALSE(groundIsException(list, n, groundSigMake(0x50, 5)));   // another sheet row
    n = groundToggleException(list, n, lakitu);
    CHECK(n == 0);
    CHECK_FALSE(groundIsException(list, n, lakitu));
    for (int i = 0; i < GROUND_EXCEPTIONS_MAX + 5; i++)
        n = groundToggleException(list, n, groundSigMake((i & 31) << 4, i >> 5));
    CHECK(n == GROUND_EXCEPTIONS_MAX);
}

TEST_CASE("ground: the sprite bottom row follows the SNES vertical wrap") {
    CHECK(groundSpriteBottom(100, 16) == 115);
    CHECK(groundSpriteBottom(230, 32) == 261);   // past the bottom: caller clamps via groundRowAt
    CHECK(groundSpriteBottom(250, 32) == 25);    // hanging off the top (VPos 250 = -6)
}

TEST_CASE("ground: touching sprites cluster, separate ones do not, invisible ones are ignored") {
    GroundBox b[5] = {
        { 100, 150, 131, 181 },   // kart body
        { 104, 134, 127, 151 },   // driver, sitting on the body (touching)
        { 100, 182, 131, 189 },   // shadow, 1 px below the body
        { 200, 40, 215, 55 },     // an opponent far away
        { 100, 150, 131, 181 },   // a duplicate box, but off screen
    };
    bool vis[5] = { true, true, true, true, false };
    uint8_t c[5];
    int n = groundClusterBoxes(b, vis, 5, 2, c);
    CHECK(n == 2);
    CHECK(c[0] == c[1]);
    CHECK(c[0] == c[2]);
    CHECK(c[3] != c[0]);
    CHECK(c[4] == 4);          // invisible: its own root, not merged
}

TEST_CASE("ground: clusters chain through a middle sprite") {
    GroundBox b[3] = { { 0, 0, 15, 15 }, { 40, 0, 55, 15 }, { 16, 0, 39, 15 } };
    bool vis[3] = { true, true, true };
    uint8_t c[3];
    CHECK(groundClusterBoxes(b, vis, 3, 0, c) == 1);
    CHECK(c[0] == c[1]);
    CHECK(c[1] == c[2]);
}

TEST_CASE("ground: sprite top follows the vertical wrap") {
    CHECK(groundSpriteTop(100) == 100);
    CHECK(groundSpriteTop(250) == -6);
}
