#include "doctest.h"
#include "../source/3dsmode7persp.h"
#include <cmath>

TEST_CASE("Mode 7 Layer never enables hidden distance effects") {
    CHECK_FALSE(mode7DistanceEffectsEnabled(true, 0, 1.0f));
    CHECK_FALSE(mode7DistanceEffectsEnabled(false, 1, 1.0f));
    CHECK_FALSE(mode7DistanceEffectsEnabled(true, 1, 0.0f));
    CHECK(mode7DistanceEffectsEnabled(true, 1, 1.0f));
}

TEST_CASE("Mode 7 squared reference preserves the original encoded depth") {
    uint32_t random = 0x7a319b05;
    for (uint32_t run = 0; run < 128; ++run) {
        float squared[512];
        float old_reference = 0.0f;
        float new_squared = 0.0f;
        for (uint32_t row = 0; row < 512; ++row) {
            random = random * 1664525u + 1013904223u;
            const float dx = static_cast<int32_t>(random & 0xffff) - 32768;
            random = random * 1664525u + 1013904223u;
            const float dy = static_cast<int32_t>(random & 0xffff) - 32768;
            squared[row] = row % 11 == 0 ? 0.0f : dx * dx + dy * dy;
            const float span = sqrtf(squared[row]);
            if (span > 0.0f && (old_reference == 0.0f || span < old_reference)) old_reference = span;
            new_squared = mode7MinPositiveSquared(new_squared, squared[row]);
        }
        const float new_reference = sqrtf(new_squared);
        CHECK(new_reference == old_reference);
        for (float square : squared) {
            CHECK(mode7PerspEncode(sqrtf(square), new_reference)
                == mode7PerspEncode(sqrtf(square), old_reference));
        }
    }
    CHECK(mode7MinPositiveSquared(0.0f, 0.0f) == 0.0f);
    CHECK(mode7MinPositiveSquared(4.0f, 0.0f) == 4.0f);
}

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

TEST_CASE("mode7 persp: gauge split - ramp first, gain second") {
    float k, g;
    mode7PerspGaugeSplit(0, &k, &g); CHECK(k == 0.0f); CHECK(g == 1.0f);
    mode7PerspGaugeSplit(2, &k, &g); CHECK(k == 0.5f); CHECK(g == 1.0f);
    mode7PerspGaugeSplit(4, &k, &g); CHECK(k == 1.0f); CHECK(g == 1.0f);
    mode7PerspGaugeSplit(6, &k, &g); CHECK(k == 1.0f); CHECK(g == 1.5f);
    mode7PerspGaugeSplit(8, &k, &g); CHECK(k == 1.0f); CHECK(g == 2.0f);
    mode7PerspGaugeSplit(99, &k, &g); CHECK(k == 1.0f); CHECK(g == 2.0f);
}

TEST_CASE("mode7 persp: fog amount follows the per-layer weights and caps") {
    CHECK(mode7FogAmount(0, 0, 1.0f) == 0.0f);
    CHECK(mode7FogAmount(8, 0, 1.0f) == doctest::Approx(0.70f));
    CHECK(mode7FogAmount(0, 8, 1.0f) == doctest::Approx(0.60f));
    CHECK(mode7FogAmount(8, 8, 1.0f) == doctest::Approx(0.85f));   // capped
    CHECK(mode7FogAmount(8, 0, 0.5f) == doctest::Approx(0.35f));   // slider scales
    CHECK(mode7FogAmount(8, 0, -1.0f) == doctest::Approx(0.70f));  // sign-free
}

TEST_CASE("mode7 persp: the right-hand vertex carries the marker plus the row's depth") {
    CHECK(mode7RightVertexY(100) == MODE7_RIGHT_MARKER);                 // depth 0, row 100
    CHECK(mode7RightVertexY(100 + 2 * 256) == MODE7_RIGHT_MARKER + 512); // depth 2
    CHECK(mode7RightVertexY(239 + 8 * 256) == MODE7_RIGHT_MARKER + 2048);
    CHECK(mode7RightVertexY(12 * 256 + 5) < -1);                        // still a marker after projection
    // the BG's alpha bits ride above the depth on the left vertex: they must
    // NOT reach the marker (16384 + marker = 0 -> the row drew as a tile)
    CHECK(mode7RightVertexY(16384 + 2 * 256 + 100) == MODE7_RIGHT_MARKER + 512);
    CHECK(mode7RightVertexY(24576 + 15 * 256 + 239) == MODE7_RIGHT_MARKER + 15 * 256);
    CHECK(mode7RightVertexY(24576 + 15 * 256 + 239) < -1);
}

TEST_CASE("mode7 extbg: the BG2 low pass is redundant only under BG1 on the same segment and window") {
    CHECK(mode7ExtbgLowPassRedundant(true, true, true, 5, 5));
    CHECK_FALSE(mode7ExtbgLowPassRedundant(false, true, true, 5, 5));   // BG1 off: BG2 low shows
    CHECK_FALSE(mode7ExtbgLowPassRedundant(true, false, true, 5, 5));   // BG1 not in this segment
    CHECK_FALSE(mode7ExtbgLowPassRedundant(true, true, false, 5, 5));   // nothing to skip
    CHECK_FALSE(mode7ExtbgLowPassRedundant(true, true, true, 5, 6));    // different window masks
}
