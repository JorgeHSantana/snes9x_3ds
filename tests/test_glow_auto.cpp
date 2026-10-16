#include "doctest.h"
#include "glow_auto.h"

TEST_CASE("glow auto: Full never drops nor fades") {
    GlowAutoState s; glowAutoReset(&s);
    for (int i = 0; i < 100; i++) glowAutoStep(&s, 0, 2);
    CHECK(s.drop == 0); CHECK(s.gainMul == 1.0f);
}

TEST_CASE("glow auto: Auto fades out, switches the drop at zero, fades back in") {
    GlowAutoState s; glowAutoReset(&s);
    glowAutoStep(&s, 1, 0); CHECK(s.drop == 0); CHECK(s.gainMul == 1.0f);
    for (int i = 0; i < GLOW_AUTO_FADE_FRAMES / 2; i++) glowAutoStep(&s, 1, 1);
    CHECK(s.drop == 0);                                // still the old level while fading
    CHECK(s.gainMul > 0.45f); CHECK(s.gainMul < 0.55f);
    for (int i = 0; i < GLOW_AUTO_FADE_FRAMES; i++) glowAutoStep(&s, 1, 1);
    CHECK(s.drop == 1);                                // switched at zero...
    CHECK(s.gainMul > 0.0f);                           // ...and already on the way back
    for (int i = 0; i < GLOW_AUTO_FADE_FRAMES; i++) glowAutoStep(&s, 1, 1);
    CHECK(s.gainMul == 1.0f);
    // load gone: the same crossfade back up to no drop
    for (int i = 0; i < 2 * GLOW_AUTO_FADE_FRAMES + 2; i++) glowAutoStep(&s, 1, 0);
    CHECK(s.drop == 0); CHECK(s.gainMul == 1.0f);
}

TEST_CASE("glow auto: a tier that flips back mid-fade never switches") {
    GlowAutoState s; glowAutoReset(&s);
    for (int i = 0; i < GLOW_AUTO_FADE_FRAMES / 2; i++) glowAutoStep(&s, 1, 2);
    for (int i = 0; i < GLOW_AUTO_FADE_FRAMES; i++) glowAutoStep(&s, 1, 0);
    CHECK(s.drop == 0); CHECK(s.gainMul == 1.0f);
}
