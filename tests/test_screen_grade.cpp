#include "doctest.h"
#include "screen_grade.h"
#include <initializer_list>

TEST_CASE("screen grade: neutral gauges are inactive, others clamp to +-4") {
    ScreenGrade g = screen_grade_compute(0, 0, 0);
    CHECK_FALSE(g.active);
    CHECK(g.sat == 1.0f); CHECK(g.con == 1.0f); CHECK(g.bri == 0.0f);
    g = screen_grade_compute(9, -9, 2);
    CHECK(g.active); CHECK(g.sat == 2.0f); CHECK(g.con == 0.5f); CHECK(g.bri == 0.125f);
}

TEST_CASE("screen grade: the contrast constants reproduce (x - 0.5) g + 0.5 + b on both paths") {
    // g <= 1: x * c1 + (c2 - 0.5)
    ScreenGrade g = screen_grade_compute(0, -4, 1);
    CHECK(g.c1 == 128); CHECK_FALSE(g.c2x2);
    CHECK(g.c5 == screen_grade_u8(0.5625f));            // brightness lives in stage 5 at s <= 1
    for (float x : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        float got = x * g.c1 / 255.0f + (g.c2 / 255.0f - 0.5f) + (g.c5 / 255.0f - 0.5f);
        float ref = (x - 0.5f) * 0.5f + 0.5f + 0.0625f;
        CHECK(got > ref - 0.01f); CHECK(got < ref + 0.01f);
    }
    g = screen_grade_compute(4, -4, 1);                   // s > 1: folded into the contrast constants
    CHECK(g.c2 == screen_grade_u8(0.5f + 0.25f + 0.0625f));
    // g > 1: (x + c1 - 0.5) * (c2 x 2)
    g = screen_grade_compute(0, 4, 0);
    CHECK(g.c2x2);
    for (float x : { 0.3f, 0.5f, 0.7f }) {
        float got = (x + g.c1 / 255.0f - 0.5f) * (g.c2 / 255.0f) * 2.0f;
        float ref = (x - 0.5f) * 1.5f + 0.5f;
        CHECK(got > ref - 0.01f); CHECK(got < ref + 0.01f);
    }
}

TEST_CASE("screen grade: saturation below 1 interpolates towards luma, above 1 pushes the channel above luma") {
    ScreenGrade g = screen_grade_compute(-4, 0, 0);          // s = 0: grey
    CHECK(g.c4 == 0);
    const float l = (76.0f * 1.0f + 150.0f * 0.0f + 29.0f * 0.0f) / 255.0f;
    CHECK(screen_grade_ref(g, 1.0f, 0.0f, 0.0f, 0) == l);
    CHECK(screen_grade_ref(g, 1.0f, 0.0f, 0.0f, 1) == l);
    g = screen_grade_compute(4, 0, 0);                       // s = 2: red gains (1 - l), green stays
    CHECK(g.c5 == 255);
    CHECK(screen_grade_ref(g, 1.0f, 0.0f, 0.0f, 0) == 1.0f);
    CHECK(screen_grade_ref(g, 0.5f, 0.0f, 0.0f, 0) > 0.5f);
    CHECK(screen_grade_ref(g, 0.5f, 0.0f, 0.0f, 1) == 0.0f);
    CHECK(screen_grade_ref(g, 0.5f, 0.5f, 0.5f, 2) == 0.5f);   // grey stays grey
}
