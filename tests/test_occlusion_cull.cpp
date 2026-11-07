#include "doctest.h"
#include "occlusion_cull.h"

TEST_CASE("occlusion_cull: a coverer must be full-opaque, unwindowed, above, and on the same segment") {
    CHECK(occl_section_covers(true, true, 7, 2, 0, 223, 0, 223));
    CHECK_FALSE(occl_section_covers(false, true, 7, 2, 0, 223, 0, 223));   // a transparent texel somewhere
    CHECK_FALSE(occl_section_covers(true, false, 7, 2, 0, 223, 0, 223));   // windowed: holes
    CHECK_FALSE(occl_section_covers(true, true, 7, 7, 0, 223, 0, 223));    // equal planes never cover
    CHECK_FALSE(occl_section_covers(true, true, 7, 8, 0, 223, 0, 223));    // the other BG is in front
    CHECK_FALSE(occl_section_covers(true, true, 7, 2, 0, 100, 0, 223));    // another segment
    // the plane rides in the vertex y: screenY + plane * 256 + alpha bits
    CHECK(occl_vertex_plane(100 + 2 * 256 + 16384) == 2);
    CHECK(occl_vertex_plane(7 + 13 * 256 + 8192) == 13);
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
    CHECK(occl_tile_hidden(8, 16, 2, 7, 0.0f, 0.0f, L, R));
    CHECK_FALSE(occl_tile_hidden(0, 8, 2, 7, 0.0f, 0.0f, L, R));      // touches the margin at x = 0
    CHECK_FALSE(occl_tile_hidden(248, 256, 2, 7, 0.0f, 0.0f, L, R));  // ...and at x = 256
    CHECK(occl_tile_hidden(240, 248, 2, 7, 0.0f, 0.0f, L, R));
    // the coverer moved right by 3, the covered layer by 1: the strip [0,3) is bare
    occl_cover_span(3.0f, 3.0f, 256, &L, &R);
    CHECK_FALSE(occl_tile_hidden(0, 8, 2, 7, 1.0f, 1.0f, L, R));
    CHECK(occl_tile_hidden(8, 16, 2, 7, 1.0f, 1.0f, L, R));           // 9 >= 4, 17 <= 258
    CHECK(occl_tile_hidden(248, 256, 2, 7, 1.0f, 1.0f, L, R));        // 257 <= 258: the coverer reaches 259
    CHECK_FALSE(occl_tile_hidden(256, 264, 2, 7, 1.0f, 1.0f, L, R));  // 265 > 258: past the coverer's edge
    // a tile partly off the left edge, nothing shifted: its -4 is outside the span
    CHECK_FALSE(occl_tile_hidden(-4, 4, 2, 7, 0.0f, 0.0f, 0.0f, 256.0f));
    // BG3 priority bit: a priority-1 tile on plane 13 stays above a plane-7 coverer
    CHECK_FALSE(occl_tile_hidden(8, 16, 13, 7, 0.0f, 0.0f, 0.0f, 256.0f));
    CHECK(occl_tile_hidden(8, 16, 5, 7, 0.0f, 0.0f, 0.0f, 256.0f));
}

TEST_CASE("occlusion_cull: the window stencil byte and the section test") {
    // inside window 1 only: bit 0 and the xor bit, shifted up by 5
    CHECK(occl_window_mask(10, 0, 20, 100, 120) == ((1 | 4) << 5));
    CHECK(occl_window_mask(110, 0, 20, 100, 120) == ((2 | 4) << 5));
    CHECK(occl_window_mask(50, 0, 20, 100, 120) == 0);
    CHECK(occl_window_mask(10, 0, 20, 0, 20) == (3 << 5));           // both: no xor
    CHECK(occl_window_mask(10, 30, 20, 100, 120) == 0);              // an empty window (left > right)
    // EQUAL ref 0x20 mask 0x20 (window 1, not inverted): passes inside only
    const unsigned eq1 = 1u | (2u << 4) | (0x20u << 16) | (0x20u << 24);
    CHECK(occl_stencil_cullable(eq1));
    CHECK(occl_stencil_pass(eq1, (1 | 4) << 5));
    CHECK_FALSE(occl_stencil_pass(eq1, 0));
    CHECK_FALSE(occl_stencil_cullable(16));                          // STENCIL_TEST_DISABLED
    CHECK_FALSE(occl_stencil_cullable(1u | (1u << 4)));              // ALWAYS: nothing to cull
    CHECK_FALSE(occl_stencil_cullable(1u));                          // NEVER: left alone
    // NOTEQUAL ref 0 mask 0x20: passes inside too, fails outside
    const unsigned ne = 1u | (3u << 4) | (0u << 16) | (0x20u << 24);
    CHECK(occl_stencil_pass(ne, 1 << 5));
    CHECK_FALSE(occl_stencil_pass(ne, 0));
}

TEST_CASE("occlusion_cull: a tile is windowed out only when every row is covered and no pixel passes") {
    const unsigned eq1 = 1u | (2u << 4) | (0x20u << 16) | (0x20u << 24);   // show inside window 1
    // window 1 = x 64..191 on rows 0..119, the whole screen on rows 120..223
    const OcclWindow w[2] = { { 0, 119, 64, 191, 300, 0 }, { 120, 223, 0, 255, 300, 0 } };
    CHECK(occl_tile_windowed_out(eq1, 8, 16, 0, 8, w, 2));        // top-left corner: outside window 1
    CHECK_FALSE(occl_tile_windowed_out(eq1, 8, 16, 60, 68, w, 2)); // straddles the left edge: 64..67 pass
    CHECK_FALSE(occl_tile_windowed_out(eq1, 8, 16, 100, 108, w, 2));
    CHECK_FALSE(occl_tile_windowed_out(eq1, 116, 124, 0, 8, w, 2)); // crosses into the full-screen rows
    CHECK(occl_tile_windowed_out(eq1, 112, 120, 200, 208, w, 2));   // rows 112..119, right of the window
    CHECK_FALSE(occl_tile_windowed_out(eq1, 220, 228, 0, 8, w, 2)); // rows past 223: no section -> unknown
    CHECK_FALSE(occl_tile_windowed_out(eq1, 8, 16, 0, 8, w, 0));    // no window sections drawn
    // a gap between sections leaves the stencil unknown
    const OcclWindow g[2] = { { 0, 99, 64, 191, 300, 0 }, { 120, 223, 0, 255, 300, 0 } };
    CHECK_FALSE(occl_tile_windowed_out(eq1, 96, 104, 0, 8, g, 2));
    // off-screen x is clipped anyway
    CHECK(occl_tile_windowed_out(eq1, 8, 16, -16, -8, w, 2));
}
