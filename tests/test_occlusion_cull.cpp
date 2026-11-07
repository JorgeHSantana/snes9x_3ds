#include "doctest.h"
#include "occlusion_cull.h"

TEST_CASE("occlusion_cull: a coverer must be full-opaque, unwindowed, above, and on the same segment") {
    CHECK(occl_section_covers(true, true, 7, 5, 0, 223, 0, 223));
    CHECK_FALSE(occl_section_covers(false, true, 7, 5, 0, 223, 0, 223));   // a transparent texel somewhere
    CHECK_FALSE(occl_section_covers(true, false, 7, 5, 0, 223, 0, 223));   // windowed: holes
    CHECK_FALSE(occl_section_covers(true, true, 7, 13, 0, 223, 0, 223));   // BG3 priority bit: its P1 above
    CHECK_FALSE(occl_section_covers(true, true, 7, 7, 0, 223, 0, 223));    // equal planes never cover
    CHECK_FALSE(occl_section_covers(true, true, 7, 5, 0, 100, 0, 223));    // another segment
}

TEST_CASE("occlusion_cull: the covered span follows the coverer's shifts") {
    float L, R;
    occl_cover_span(0.0f, 0.0f, 256, &L, &R);
    CHECK(L == 0.0f); CHECK(R == 256.0f);
    occl_cover_span(3.0f, 3.0f, 256, &L, &R);      // the layer moved right: the left 3 px are bare
    CHECK(L == 3.0f); CHECK(R == 259.0f);
    occl_cover_span(-2.0f, 1.0f, 256, &L, &R);     // mixed priorities: only the intersection is sure
    CHECK(L == 1.0f); CHECK(R == 254.0f);
}

TEST_CASE("occlusion_cull: in 2D every tile hides, in 3D the edge strip stays") {
    float L, R;
    occl_cover_span(0.0f, 0.0f, 256, &L, &R);
    CHECK(occl_tile_hidden(8, 16, 0.0f, 0.0f, L, R));
    CHECK_FALSE(occl_tile_hidden(0, 8, 0.0f, 0.0f, L, R));      // touches the margin at x = 0
    CHECK_FALSE(occl_tile_hidden(248, 256, 0.0f, 0.0f, L, R));  // ...and at x = 256
    CHECK(occl_tile_hidden(240, 248, 0.0f, 0.0f, L, R));
    // the coverer moved right by 3, the covered layer by 1: the strip [0,3) is bare
    occl_cover_span(3.0f, 3.0f, 256, &L, &R);
    CHECK_FALSE(occl_tile_hidden(0, 8, 1.0f, 1.0f, L, R));
    CHECK(occl_tile_hidden(8, 16, 1.0f, 1.0f, L, R));           // 9 >= 4, 17 <= 258
    CHECK(occl_tile_hidden(248, 256, 1.0f, 1.0f, L, R));        // 257 <= 258: the coverer reaches 259
    CHECK_FALSE(occl_tile_hidden(256, 264, 1.0f, 1.0f, L, R));  // 265 > 258: past the coverer's edge
    // a tile partly off the left edge, nothing shifted: its -4 is outside the span
    CHECK_FALSE(occl_tile_hidden(-4, 4, 0.0f, 0.0f, 0.0f, 256.0f));
}
