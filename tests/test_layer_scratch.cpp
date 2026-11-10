#include "doctest.h"
#include "../source/layer_scratch.h"

namespace {
struct V { short x, y, z, u, v; };
// one 8x8 tile at (x, row) on a plane with alpha bits
void tile(V *v, int x, int row, int plane, int aa = 0, int w = 8, int h = 8)
{
    v[0].x = (short)x; v[0].y = (short)(row + plane * 256 + aa);
    v[1].x = (short)(x + w); v[1].y = (short)(v[0].y + h);
    v[0].z = v[1].z = 1; v[0].u = v[0].v = v[1].u = v[1].v = 0;
}
}

TEST_CASE("layer_scratch: one plane - bounding box, tile count and area") {
    V v[6];
    tile(&v[0], -3, 0, 2); tile(&v[2], 5, 0, 2); tile(&v[4], 253, 216, 2);
    LayerScratchScan s;
    CHECK(layer_scratch_scan(&v[0].x, sizeof(V), 6, &s));
    CHECK(s.nPlanes == 1);
    CHECK(s.p[0].plane == 2);
    CHECK(s.p[0].aa == 0);
    CHECK(s.p[0].x0 == -3); CHECK(s.p[0].x1 == 261);
    CHECK(s.p[0].y0 == 0);  CHECK(s.p[0].y1 == 224);
    CHECK(s.p[0].tiles == 3);
    CHECK(s.p[0].tilePx == 192u);
    CHECK(layer_scratch_bbox_px(&s) == 264u * 224u);
    CHECK(layer_scratch_tile_px(&s) == 192u);
}

TEST_CASE("layer_scratch: two planes come out ascending, each with its own box and alpha bits") {
    V v[8];
    tile(&v[0], 0, 8, 5, 0x4000); tile(&v[2], 16, 16, 2, 0x2000);
    tile(&v[4], 8, 8, 5, 0x4000); tile(&v[6], 24, 32, 2, 0x2000);
    LayerScratchScan s;
    CHECK(layer_scratch_scan(&v[0].x, sizeof(V), 8, &s));
    CHECK(s.nPlanes == 2);
    CHECK(s.p[0].plane == 2); CHECK(s.p[0].aa == 0x2000);
    CHECK(s.p[0].x0 == 16); CHECK(s.p[0].x1 == 32); CHECK(s.p[0].y0 == 16); CHECK(s.p[0].y1 == 40);
    CHECK(s.p[1].plane == 5); CHECK(s.p[1].aa == 0x4000);
    CHECK(s.p[1].x0 == 0); CHECK(s.p[1].x1 == 16); CHECK(s.p[1].y0 == 8); CHECK(s.p[1].y1 == 16);
    CHECK(s.p[0].tiles == 2); CHECK(s.p[1].tiles == 2);
}

TEST_CASE("layer_scratch: what the image cannot stand in for") {
    V v[6]; LayerScratchScan s;
    // three planes
    tile(&v[0], 0, 0, 1); tile(&v[2], 8, 0, 2); tile(&v[4], 16, 0, 3);
    CHECK_FALSE(layer_scratch_scan(&v[0].x, sizeof(V), 6, &s));
    // alpha bits differ inside a plane
    tile(&v[0], 0, 0, 1, 0x2000); tile(&v[2], 8, 0, 1, 0x4000);
    CHECK_FALSE(layer_scratch_scan(&v[0].x, sizeof(V), 4, &s));
    // a tile crossing row 255 (its bottom vertex would decode on the next plane)
    tile(&v[0], 0, 250, 1);
    CHECK_FALSE(layer_scratch_scan(&v[0].x, sizeof(V), 2, &s));
    tile(&v[0], 0, 247, 1);
    CHECK(layer_scratch_scan(&v[0].x, sizeof(V), 2, &s));
    // negative y, degenerate tiles
    tile(&v[0], 0, 0, 0); v[0].y = -4; v[1].y = 4;
    CHECK_FALSE(layer_scratch_scan(&v[0].x, sizeof(V), 2, &s));
    tile(&v[0], 0, 0, 0, 0, 8, 0);
    CHECK_FALSE(layer_scratch_scan(&v[0].x, sizeof(V), 2, &s));
    tile(&v[0], 0, 0, 0, 0, 0, 8);
    CHECK_FALSE(layer_scratch_scan(&v[0].x, sizeof(V), 2, &s));
    // outside the image
    tile(&v[0], -129, 0, 0);
    CHECK_FALSE(layer_scratch_scan(&v[0].x, sizeof(V), 2, &s));
    tile(&v[0], -128, 0, 0);
    CHECK(layer_scratch_scan(&v[0].x, sizeof(V), 2, &s));
    tile(&v[0], 377, 0, 0);
    CHECK_FALSE(layer_scratch_scan(&v[0].x, sizeof(V), 2, &s));
    tile(&v[0], 376, 0, 0);
    CHECK(layer_scratch_scan(&v[0].x, sizeof(V), 2, &s));
    // no vertices, an odd count
    CHECK_FALSE(layer_scratch_scan(&v[0].x, sizeof(V), 0, &s));
    CHECK_FALSE(layer_scratch_scan(&v[0].x, sizeof(V), 3, &s));
}

TEST_CASE("layer_scratch: a full BG drawn many times pays, a single pass or a sparse layer does not") {
    const unsigned int full = 264u * 224u;
    // MMX3 Zero Project, light phase: 957 tiles, 12 passes
    CHECK(layer_scratch_pays(1914, 12, 1, full, 957u * 64u));
    CHECK(layer_scratch_pays(1914, 6, 1, full, 957u * 64u));
    // two planes sharing the screen in halves: the boxes add up to one screen
    CHECK(layer_scratch_pays(1914, 12, 2, full, 957u * 64u));
    // two planes interleaved over the whole screen: every pass fills the screen twice, the tiles win
    CHECK_FALSE(layer_scratch_pays(1914, 12, 2, 2u * full, 957u * 64u));
    // one pass: the build alone costs as much
    CHECK_FALSE(layer_scratch_pays(1914, 1, 1, full, 957u * 64u));
    // 20 tiles spread over the screen: the box fill outweighs 40 vertices
    CHECK_FALSE(layer_scratch_pays(40, 12, 1, full, 20u * 64u));
    // the same 20 tiles in one row
    CHECK(layer_scratch_pays(40, 12, 1, 160u * 8u, 20u * 64u));
    CHECK_FALSE(layer_scratch_pays(0, 12, 1, full, 0));
    CHECK_FALSE(layer_scratch_pays(1914, 12, 0, full, 957u * 64u));
}

TEST_CASE("layer_scratch: a plane's tier follows the layer's boundary") {
    CHECK(layer_scratch_tier(2, 3.5f / 32.0f) == 0);
    CHECK(layer_scratch_tier(5, 3.5f / 32.0f) == 1);
    CHECK(layer_scratch_tier(4, 4.0f / 32.0f) == 1);
    CHECK(layer_scratch_tier(4, 0.0f) == 1);
}
