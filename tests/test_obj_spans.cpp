#include "doctest.h"
#include "obj_spans.h"

TEST_CASE("obj spans: small sprites of a target come back merged when adjacent") {
    ObjSpanTable t; t.reset();
    t.add(100, 4, false, false);
    t.add(104, 6, false, false);      // adjacent: merges
    t.add(110, 8, true, false);       // large: skipped
    t.add(118, 2, false, false);      // not adjacent to the merged run (110 gap)
    t.add(120, 2, false, true);       // other target
    ObjSpan out[8];
    int m = t.small_ranges(false, out, 8);
    CHECK(m == 2);
    CHECK(out[0].from == 100); CHECK(out[0].count == 10);
    CHECK(out[1].from == 118); CHECK(out[1].count == 2);
    CHECK(t.small_ranges(true, out, 8) == 1);
    CHECK(out[0].from == 120);
}

TEST_CASE("obj spans: empty spans are ignored, the table caps at 128 and the output at cap") {
    ObjSpanTable t; t.reset();
    t.add(5, 0, false, false);
    CHECK(t.n == 0);
    for (int i = 0; i < 200; i++) t.add(i * 4, 2, false, false);   // non-adjacent (gap of 2)
    CHECK(t.n == 128);
    ObjSpan out[4];
    CHECK(t.small_ranges(false, out, 4) == 4);
}

TEST_CASE("obj spans: touching sprites form one cluster anchored at the union's feet") {
    ObjSpanTable t; t.reset();
    t.add(100, 4, true, false, 10, 10, 41, 41);     // 32x32 head
    t.add(104, 4, true, false, 10, 42, 41, 73);     // 32x32 body right under it: touches
    t.add(108, 2, false, false, 200, 50, 207, 57);  // far away small sprite
    t.add(110, 2, false, true, 10, 10, 17, 17);     // other target
    t.add(112, 2, false, false, 42, 60, 49, 67);    // touches the body on its right edge
    uint8_t of[ObjSpanTable::MAX]; ObjCluster cl[8];
    CHECK(t.clusters(false, false, of, cl, 8) == 2);
    CHECK(of[0] == 0); CHECK(of[1] == 0); CHECK(of[4] == 0);
    CHECK(of[2] == 1);
    CHECK(of[3] == 0xFF);
    CHECK(cl[0].ax == 30); CHECK(cl[0].ay == 74);   // union 10..49 x 10..73
    CHECK(cl[1].ax == 204); CHECK(cl[1].ay == 58);
    // small only: the large head and body drop out, the small ones remain
    CHECK(t.clusters(false, true, of, cl, 8) == 2);
    CHECK(of[0] == 0xFF); CHECK(of[2] == 0); CHECK(of[4] == 1);
}

TEST_CASE("obj spans: a sprite bridging two clusters merges them") {
    ObjSpanTable t; t.reset();
    t.add(0, 2, false, false, 0, 0, 7, 7);
    t.add(2, 2, false, false, 20, 0, 27, 7);
    t.add(4, 2, false, false, 8, 0, 19, 7);         // touches both
    uint8_t of[ObjSpanTable::MAX]; ObjCluster cl[8];
    CHECK(t.clusters(false, false, of, cl, 8) == 2);   // ids 0 and 1 were created; 1 is now empty
    CHECK(of[0] == of[1]); CHECK(of[1] == of[2]);
    CHECK(cl[of[0]].ax == 14); CHECK(cl[of[0]].ay == 8);
}
