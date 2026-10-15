#include "doctest.h"
#include "stereo_glow.h"

TEST_CASE("stereo glow: threshold maps to an alpha reference that never passes transparent texels") {
    CHECK(stereo_glow_alpha_ref(0) == 1);
    CHECK(stereo_glow_alpha_ref(8) == 128);
    CHECK(stereo_glow_alpha_ref(15) == 240);
    CHECK(stereo_glow_alpha_ref(99) == 240);
}

TEST_CASE("stereo glow: copies cover the centre, the cross and the 3x3 box") {
    CHECK(stereo_glow_copies(0) == 1); CHECK(stereo_glow_copies(1) == 5); CHECK(stereo_glow_copies(2) == 9);
    int seen = 0;
    for (int c = 0; c < 5; c++) seen |= 1 << ((stereo_glow_dx(1, c) + 1) + (stereo_glow_dy(1, c) + 1) * 3);
    CHECK(seen == 0x0BA);   // centre + 4 neighbours of the 3x3 grid (bits 1,3,4,5,7)
    seen = 0;
    for (int c = 0; c < 9; c++) seen |= 1 << ((stereo_glow_dx(2, c) + 1) + (stereo_glow_dy(2, c) + 1) * 3);
    CHECK(seen == 0x1FF);
    CHECK(stereo_glow_dx(2, 0) == 0); CHECK(stereo_glow_dy(2, 0) == 0);
}

TEST_CASE("stereo glow: the per-copy gain sums to the strength at the centre, x2 scale above 1") {
    CHECK(stereo_glow_copy_gain(4, 0) == 1.0f);          // x1 in place
    CHECK(stereo_glow_copy_gain(8, 0) == 2.0f);          // x2 in place: constant 255 with the x2 scale
    CHECK(stereo_glow_env_x2(2.0f)); CHECK(stereo_glow_env_const(2.0f) == 255);
    CHECK_FALSE(stereo_glow_env_x2(0.5f)); CHECK(stereo_glow_env_const(0.5f) == 128);
    const float g = stereo_glow_copy_gain(4, 2);
    CHECK(g > 0.11f); CHECK(g < 0.12f);                   // 1.0 / 9 copies
    CHECK(stereo_glow_copy_gain(0, 0) == 0.25f);          // strength clamps to 1
}
