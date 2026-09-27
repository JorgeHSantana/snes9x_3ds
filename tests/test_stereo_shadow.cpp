#include "doctest.h"
#include "stereo_shadow.h"
#include <string.h>

TEST_CASE("stereo shadow: rows map to bits in Depth-list order and reject what is not a row") {
    CHECK(stereo_shadow_row(0, 0) == 0);
    CHECK(stereo_shadow_row(0, 1) == 1);
    CHECK(stereo_shadow_row(3, 1) == 7);
    CHECK(stereo_shadow_row(4, 0) == 8);
    CHECK(stereo_shadow_row(4, 3) == 11);
    CHECK(stereo_shadow_row(2, 2) == -1);
    CHECK(stereo_shadow_row(4, 4) == -1);
    CHECK(stereo_shadow_row(5, 0) == -1);
    CHECK(stereo_shadow_marked(1u << 9, 4, 1));
    CHECK_FALSE(stereo_shadow_marked(1u << 9, 4, 0));
    CHECK_FALSE(stereo_shadow_marked(0xFFF, 2, 2));
}

TEST_CASE("stereo shadow: the shadow takes the nearest depth behind the row, own depth when nothing is behind") {
    int d[12] = { -6, -6, -3, -3, 0, 0, 2, 2, 4, 4, 4, 4 };
    CHECK(stereo_shadow_behind_depth(d, 0, 8) == 2);     // sprites at 4: BG4 at 2 is the surface
    CHECK(stereo_shadow_behind_depth(d, 0, 6) == 0);     // BG4 at 2: BG3 at 0
    CHECK(stereo_shadow_behind_depth(d, 0, 0) == -6);    // BG1 P0 at -6: nothing behind, own depth
    CHECK(stereo_shadow_behind_depth(d, 0, 1) == -6);    // BG1 P1 shares -6 with P0: not behind
}

TEST_CASE("stereo shadow: only rows the scene uses count as a surface") {
    int d[12] = { -6, -6, -3, -3, 0, 0, 2, 2, 4, 4, 4, 4 };
    const unsigned used = (1u << 0) | (1u << 8);         // BG1 P0 and sprites P0 only
    CHECK(stereo_shadow_behind_depth(d, used, 8) == -6); // BG4/BG3 are not drawn: BG1 is the surface
    CHECK(stereo_shadow_behind_depth(d, used, 0) == -6);
    CHECK(stereo_shadow_behind_depth(d, used, 99) == 0);
}

TEST_CASE("stereo shadow: the offset gauge clamps to +-4") {
    CHECK(stereo_shadow_clamp_offset(9) == 4);
    CHECK(stereo_shadow_clamp_offset(-9) == -4);
    CHECK(stereo_shadow_clamp_offset(2) == 2);
}

TEST_CASE("stereo shadow: the colour table is indexed safely and starts black") {
    CHECK(stereo_shadow_color_rgb(0) == 0x000000);
    CHECK(stereo_shadow_color_rgb(5) == 0xFFFFFF);
    CHECK(stereo_shadow_color_rgb(-1) == 0x000000);
    CHECK(stereo_shadow_color_rgb(99) == 0x000000);
    CHECK(strcmp(stereo_shadow_color_name(1), "Dark gray") == 0);
}

TEST_CASE("stereo shadow: the behind row is the used row nearest below, -1 when none") {
    int d[12] = { -6, -6, -3, -3, 0, 0, 2, 2, 4, 4, 4, 4 };
    CHECK(stereo_shadow_behind_row(d, 0, 8) == 6);       // sprites at 4: BG4 P0 at 2 (first of the pair)
    CHECK(stereo_shadow_behind_row(d, (1u << 0) | (1u << 8), 8) == 0);
    CHECK(stereo_shadow_behind_row(d, 0, 0) == -1);
    CHECK(stereo_shadow_behind_row(d, 0, 99) == -1);
}
