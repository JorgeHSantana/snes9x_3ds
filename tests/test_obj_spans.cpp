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
