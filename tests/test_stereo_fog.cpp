#include "doctest.h"
#include "stereo_fog.h"

TEST_CASE("stereo_fog_amount: gauge x depth share x slider, clamped at 0.85") {
    CHECK(stereo_fog_amount(0.0f, 0.0f, 1.0f, 1.0f) == 0.0f);
    CHECK(stereo_fog_amount(8.0f, 0.0f, 1.0f, 1.0f) == doctest::Approx(0.70f));
    CHECK(stereo_fog_amount(0.0f, 8.0f, 1.0f, 1.0f) == doctest::Approx(0.60f));
    CHECK(stereo_fog_amount(8.0f, 8.0f, 1.0f, 1.0f) == doctest::Approx(0.85f));   // 1.30 clamped
    CHECK(stereo_fog_amount(4.0f, 0.0f, 0.5f, 1.0f) == doctest::Approx(0.175f));
    CHECK(stereo_fog_amount(4.0f, 0.0f, 1.0f, -0.5f) == doctest::Approx(0.175f));   // the right eye's negative IOD
    CHECK(stereo_fog_amount(8.0f, 8.0f, 0.0f, 1.0f) == 0.0f);   // inside the focus zone
}

TEST_CASE("stereo_fog_share: the haze fraction, depth-independent") {
    CHECK(stereo_fog_share(0.0f, 0.0f) == 0.0f);
    CHECK(stereo_fog_share(8.0f, 0.0f) == 0.0f);
    CHECK(stereo_fog_share(0.0f, 8.0f) == 1.0f);
    CHECK(stereo_fog_share(6.0f, 6.0f) == doctest::Approx(3.6f / (4.2f + 3.6f)));
}

TEST_CASE("stereo_fog_color: opaque ABGR, black for fade, the haze tint for haze") {
    CHECK(stereo_fog_color(0.0f) == 0xFF000000u);
    CHECK(stereo_fog_color(1.0f) == (0xFF000000u | (215u << 16) | (205u << 8) | 200u));
    CHECK(stereo_fog_color(0.5f) == (0xFF000000u | (107u << 16) | (102u << 8) | 100u));
}
