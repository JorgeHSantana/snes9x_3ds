#include "doctest.h"
#include "glow_relative.h"

static uint16_t grey(int v) { const uint16_t c = (uint16_t)(v >> 3); return (uint16_t)(c | (c << 5) | (c << 10)); }

TEST_CASE("glow relative: luma of the palette entries uses the TexEnv weights") {
    CHECK(glow_rel_luma(0) == 0);
    CHECK(glow_rel_luma(0x7FFF) == 248);          // 31<<3 = 248 on every channel
    CHECK(glow_rel_luma(0x03E0) > glow_rel_luma(0x001F));   // green weighs more than red
}

TEST_CASE("glow relative: the bright is the 90th percentile, so one HUD white does not win") {
    uint16_t cg[256];
    for (int i = 0; i < 256; i++) cg[i] = grey(96);
    for (int i = 0; i < 8; i++) cg[i] = 0x7FFF;          // 8 white entries (3%)
    CHECK(glow_rel_bright(cg, 256) < 110);
    CHECK(glow_rel_bright(cg, 256) >= 88);
    for (int i = 0; i < 40; i++) cg[i] = 0x7FFF;         // 16%: now the bright is white
    CHECK(glow_rel_bright(cg, 256) == 255);
}

TEST_CASE("glow relative: a dark scene lowers the cut and raises the gain, capped") {
    CHECK(glow_rel_ref(12, 255.0f) == 191);                  // bright scene: ~the absolute 192
    CHECK(glow_rel_ref(12, 120.0f) == 90);                   // dark scene: 75% of 120
    CHECK(glow_rel_ref(0, 120.0f) == 1);                     // never 0 (transparent texels)
    CHECK(glow_rel_gain(255.0f) == 1.0f);
    CHECK(glow_rel_gain(170.0f) == doctest::Approx(1.5f));
    CHECK(glow_rel_gain(60.0f) == 2.0f);                     // capped
}

TEST_CASE("glow relative: the bright is tracked, not jumped, and never chases black") {
    GlowRelState s; glowRelReset(&s);
    glowRelStep(&s, 55);
    CHECK(s.bright > 240.0f);                                // one frame moves 1/20 of the way
    for (int i = 0; i < 200; i++) glowRelStep(&s, 55);
    CHECK(s.bright == doctest::Approx(55.0f).epsilon(0.01));
    for (int i = 0; i < 200; i++) glowRelStep(&s, 0);
    CHECK(s.bright == doctest::Approx((float)GLOW_REL_MIN_BRIGHT).epsilon(0.01));
}
