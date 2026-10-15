#include "doctest.h"
#include "shadow_auto.h"

TEST_CASE("shadow auto: Full ignores the load, Auto drops the soft level one step per tier") {
    ShadowAutoState s; shadowAutoReset(&s, 2);
    shadowAutoStep(&s, 0, 2, 2);
    CHECK(s.level == 2); CHECK(s.alphaMul == 1.0f);
    shadowAutoStep(&s, 1, 0, 2); CHECK(s.level == 2);
    shadowAutoStep(&s, 1, 1, 2); CHECK(s.level == 1);
    shadowAutoStep(&s, 1, 2, 2); CHECK(s.level == 0);
    shadowAutoStep(&s, 1, 2, 1); CHECK(s.level == 0);
    CHECK(s.alphaMul == 1.0f);                       // Auto never fades the shadow itself
}

TEST_CASE("shadow auto: Off under load fades out over the fade frames and back in") {
    ShadowAutoState s; shadowAutoReset(&s, 1);
    for (int i = 0; i < SHADOW_AUTO_FADE_FRAMES / 2; i++) shadowAutoStep(&s, 2, 2, 1);
    CHECK(s.level == 0);
    CHECK(s.alphaMul > 0.45f); CHECK(s.alphaMul < 0.55f);
    for (int i = 0; i < SHADOW_AUTO_FADE_FRAMES; i++) shadowAutoStep(&s, 2, 2, 1);
    CHECK(s.alphaMul == 0.0f);
    for (int i = 0; i < SHADOW_AUTO_FADE_FRAMES / 2; i++) shadowAutoStep(&s, 2, 1, 1);
    CHECK(s.level == 0);                             // Light: soft edge still off
    CHECK(s.alphaMul > 0.45f); CHECK(s.alphaMul < 0.55f);
    for (int i = 0; i < SHADOW_AUTO_FADE_FRAMES; i++) shadowAutoStep(&s, 2, 0, 1);
    CHECK(s.level == 1); CHECK(s.alphaMul == 1.0f);
}
