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

TEST_CASE("mode7 persp: the shader factor is an exact no-op at strength 0 and at the nearest row") {
    CHECK(mode7PerspFactor(8, 0.0f) == 1.0f);
    CHECK(mode7PerspFactor(1, 0.0f) == 1.0f);        // a tile's loader-filled w
    CHECK(mode7PerspFactor(MODE7_PERSP_W_ONE, 1.0f) == 1.0f);
    CHECK(mode7PerspFactor(128, 1.0f) == doctest::Approx(0.5f));
    CHECK(mode7PerspFactor(128, 0.5f) == doctest::Approx(0.75f));
}
