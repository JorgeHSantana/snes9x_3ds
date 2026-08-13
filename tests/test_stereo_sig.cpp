#include "doctest.h"
#include "../source/3dsstereosig.h"

// helpers to pack the 7-byte signature words like the runtime does
static uint64_t pack(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3,
                     uint8_t b4, uint8_t b5, uint8_t b6)
{
    return (uint64_t)b0 | ((uint64_t)b1 << 8) | ((uint64_t)b2 << 16)
        | ((uint64_t)b3 << 24) | ((uint64_t)b4 << 32)
        | ((uint64_t)b5 << 40) | ((uint64_t)b6 << 48);
}

TEST_CASE("capture mask: stable scene keeps every register byte")
{
    uint64_t sig = pack(0x09, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00);
    CHECK(stereoSigCapMask(sig, sig) == STEREO_SIG_BYTES);
}

TEST_CASE("capture mask: a flapping byte is excluded, the rest kept")
{
    // MMX3 title: 2131 (byte 4) blinks 00<->BF, TM (byte 1) flips 13<->17
    uint64_t andv = pack(0x09, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00);
    uint64_t orv  = pack(0x09, 0x17, 0x00, 0x00, 0xBF, 0x00, 0x00);
    uint64_t mask = stereoSigCapMask(orv, andv);
    CHECK((mask & (0xFFULL << 8))  == 0);                    // TM masked out
    CHECK((mask & (0xFFULL << 32)) == 0);                    // 2131 masked out
    CHECK((mask & 0xFFULL) == 0xFFULL);                      // 2105 kept
    CHECK((mask & (0xFFULL << 48)) == (0xFFULL << 48));      // 420C kept
}

TEST_CASE("capture mask: bit 56+ padding never participates")
{
    CHECK((stereoSigCapMask(~0ULL, ~0ULL) & ~STEREO_SIG_BYTES) == 0);
}

TEST_CASE("bind match: exact signature hits, one differing byte misses")
{
    uint64_t sig = pack(0x09, 0x13, 0, 0, 0, 0, 0);
    CHECK(stereoSigBindMatches(sig, 0, -1, sig, STEREO_SIG_BYTES, 0, 0, -1));
    uint64_t other = pack(0x01, 0x13, 0, 0, 0, 0, 0);
    CHECK_FALSE(stereoSigBindMatches(other, 0, -1, sig, STEREO_SIG_BYTES, 0, 0, -1));
}

TEST_CASE("bind match: masked byte differences are ignored")
{
    // title bind with TM and 2131 masked out (the learned mask)
    uint64_t bSig  = pack(0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
    uint64_t bMask = pack(0xFF, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0xFF);
    uint64_t blinkFrame = pack(0x09, 0x17, 0x00, 0x00, 0xBF, 0x00, 0x00);
    CHECK(stereoSigBindMatches(blinkFrame, 0, -1, bSig, bMask, 0, 0, -1));
}

TEST_CASE("bind match: WRAM watch is the arbiter for PPU-twin scenes")
{
    // MMX3: title and stage start share the PPU tuple; 7E00F1 differs
    // (06 title, 04 in-game)
    uint64_t twin  = pack(0x09, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00);
    uint64_t bMask = pack(0xFF, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0xFF);
    CHECK(stereoSigBindMatches(twin, 0, 0x06, twin, bMask, 0, 0, 0x06));  // title
    CHECK_FALSE(stereoSigBindMatches(twin, 0, 0x04, twin, bMask, 0, 0, 0x06)); // stage blocked
}

TEST_CASE("bind match: bind without watch ignores the watch byte")
{
    uint64_t sig = pack(0x01, 0x17, 0x17, 0x10, 0x42, 0x00, 0xD8);
    CHECK(stereoSigBindMatches(sig, 0, 0x04, sig, STEREO_SIG_BYTES, 0, 0, -1));
}

TEST_CASE("bind match: zero word2 mask ignores the VRAM bases entirely")
{
    uint64_t sig  = pack(0x09, 0x13, 0, 0, 0, 0, 0);
    uint64_t sig2 = pack(0x03, 0x51, 0x59, 0x0A, 0x00, 0x11, 0x00);
    CHECK(stereoSigBindMatches(sig, sig2, -1, sig, STEREO_SIG_BYTES, 0, 0, -1));
    // and with a real word2 mask, a differing base misses
    uint64_t otherBases = pack(0x03, 0x60, 0x59, 0x0A, 0x00, 0x11, 0x00);
    CHECK_FALSE(stereoSigBindMatches(sig, otherBases, -1,
        sig, STEREO_SIG_BYTES, sig2, STEREO_SIG_BYTES, -1));
}
