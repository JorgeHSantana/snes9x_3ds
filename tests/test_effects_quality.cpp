#include "doctest.h"
#include "effects_quality.h"

TEST_CASE("effects quality: every preset round-trips through its triple") {
    for (int p = 0; p < EFFECTS_QUALITY_MIXED; p++) {
        int s = -1, g = -1, b = -1;
        CHECK(effects_quality_values(p, &s, &g, &b));
        CHECK(effects_quality_preset(s, g, b) == p);
    }
}

TEST_CASE("effects quality: a triple no preset owns reads as Mixed, and Mixed has no values") {
    CHECK(effects_quality_preset(0, 1, 0) == EFFECTS_QUALITY_MIXED);   // Full shadows, Auto glow and blur
    CHECK(effects_quality_preset(2, 0, 2) == EFFECTS_QUALITY_MIXED);
    int s = 7, g = 7, b = 7;
    CHECK_FALSE(effects_quality_values(EFFECTS_QUALITY_MIXED, &s, &g, &b));
    CHECK(s == 7);   // untouched
}

TEST_CASE("effects quality: Full and Light pin every controller, the Autos leave the lowering to load") {
    int s, g, b;
    effects_quality_values(EFFECTS_QUALITY_FULL, &s, &g, &b);
    CHECK(s == 0); CHECK(g == 0); CHECK(b == 1);
    effects_quality_values(EFFECTS_QUALITY_LIGHT, &s, &g, &b);
    CHECK(b == 2);
    effects_quality_values(EFFECTS_QUALITY_AUTO_OFF, &s, &g, &b);
    CHECK(s == 2); CHECK(b == 3);
}
