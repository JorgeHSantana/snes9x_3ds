#include "doctest.h"
#include "../source/3dsmode7persp.h"

TEST_CASE("mode7 persp: the nearest row keeps the full shift") {
    CHECK(mode7PerspEncode(2.0f, 2.0f) == MODE7_PERSP_W_ONE);
}

TEST_CASE("mode7 persp: a uniform span (top-down map) is flat everywhere") {
    for (int i = 0; i < 224; i++)
        CHECK(mode7PerspEncode(1.5f, 1.5f) == MODE7_PERSP_W_ONE);
}

TEST_CASE("mode7 persp: farther rows (more texels per pixel) shrink monotonically") {
    int16_t prev = MODE7_PERSP_W_ONE;
    for (float span = 1.0f; span < 64.0f; span *= 1.5f) {
        int16_t w = mode7PerspEncode(span, 1.0f);
        CHECK(w <= prev);
        prev = w;
    }
    CHECK(mode7PerspEncode(2.0f, 1.0f) == 128);
    CHECK(mode7PerspEncode(4.0f, 1.0f) == 64);
}

TEST_CASE("mode7 persp: clamps and guards") {
    CHECK(mode7PerspEncode(1000000.0f, 1.0f) == MODE7_PERSP_W_MIN);   // horizon floor
    CHECK(mode7PerspEncode(0.5f, 1.0f) == MODE7_PERSP_W_ONE);         // nearer than the reference: no boost
    CHECK(mode7PerspEncode(0.0f, 1.0f) == MODE7_PERSP_W_ONE);         // degenerate span
    CHECK(mode7PerspEncode(1.0f, 0.0f) == MODE7_PERSP_W_ONE);         // no reference yet
}

TEST_CASE("mode7 persp: row shift is an exact no-op with no recession and at the nearest row") {
    CHECK(mode7PerspRowShift(8, -4.0f, 0.0f) == -4.0f);
    CHECK(mode7PerspRowShift(1, 0.0f, 0.0f) == 0.0f);          // a tile's loader-filled w
    CHECK(mode7PerspRowShift(MODE7_PERSP_W_ONE, -4.0f, -4.0f) == -4.0f);
}

TEST_CASE("mode7 persp: the horizon sinks past the gauge whatever its sign") {
    // negative gauge (into the screen): the horizon is deeper still
    CHECK(mode7PerspRowShift(128, -4.0f, -4.0f) == doctest::Approx(-6.0f));
    CHECK(mode7PerspRowShift(MODE7_PERSP_W_MIN, -4.0f, -4.0f) == doctest::Approx(-7.875f));
    // positive gauge (pop out): the near rows pop, the horizon still recedes
    CHECK(mode7PerspRowShift(128, 4.0f, -4.0f) == doctest::Approx(2.0f));
    CHECK(mode7PerspRowShift(MODE7_PERSP_W_MIN, 4.0f, -4.0f) < 4.0f);
    // rows never move against the recession
    float prev = mode7PerspRowShift(MODE7_PERSP_W_ONE, 2.0f, -3.0f);
    for (int w = MODE7_PERSP_W_ONE - 8; w >= MODE7_PERSP_W_MIN; w -= 8) {
        float cur = mode7PerspRowShift((int16_t)w, 2.0f, -3.0f);
        CHECK(cur <= prev);
        prev = cur;
    }
}

TEST_CASE("mode7 persp: gauge is the recession in depth units, clamped 0..8") {
    CHECK(mode7PerspRecede(0) == 0.0f);
    CHECK(mode7PerspRecede(4) == 4.0f);
    CHECK(mode7PerspRecede(8) == 8.0f);
    CHECK(mode7PerspRecede(99) == 8.0f);
    CHECK(mode7PerspRecede(-3) == 0.0f);
}

TEST_CASE("mode7 persp: fog amount follows the per-layer weights and caps") {
    CHECK(mode7FogAmount(0, 0, 1.0f) == 0.0f);
    CHECK(mode7FogAmount(8, 0, 1.0f) == doctest::Approx(0.70f));
    CHECK(mode7FogAmount(0, 8, 1.0f) == doctest::Approx(0.60f));
    CHECK(mode7FogAmount(8, 8, 1.0f) == doctest::Approx(0.85f));   // capped
    CHECK(mode7FogAmount(8, 0, 0.5f) == doctest::Approx(0.35f));   // slider scales
    CHECK(mode7FogAmount(8, 0, -1.0f) == doctest::Approx(0.70f));  // sign-free
}
