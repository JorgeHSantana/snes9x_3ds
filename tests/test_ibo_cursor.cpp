#include "doctest.h"
#include "ibo_cursor.h"

TEST_CASE("ibo cursor: consecutive passes get disjoint ranges") {
    unsigned int cur = 0;
    CHECK(ibo_cursor_take(&cur, 378, 1000) == 0);
    CHECK(ibo_cursor_take(&cur, 378, 1000) == 378);
    CHECK(cur == 756);
}

TEST_CASE("ibo cursor: a range that does not fit is refused and the cursor stays") {
    unsigned int cur = 900;
    CHECK(ibo_cursor_take(&cur, 101, 1000) == -1);
    CHECK(cur == 900);
    CHECK(ibo_cursor_take(&cur, 100, 1000) == 900);
    CHECK(cur == 1000);
    CHECK(ibo_cursor_take(&cur, 1, 1000) == -1);
    CHECK(ibo_cursor_take(&cur, 0, 1000) == 1000);
}

TEST_CASE("ibo cursor: a cursor past the capacity never wraps") {
    unsigned int cur = 1200;
    CHECK(ibo_cursor_take(&cur, 1, 1000) == -1);
    CHECK(cur == 1200);
}

TEST_CASE("ibo cursor: the floor keeps the fixed ranges out of reach") {
    unsigned int cur = 0;
    ibo_cursor_floor(&cur, 4000);
    CHECK(cur == 4000);
    CHECK(ibo_cursor_take(&cur, 10, 5000) == 4000);
    ibo_cursor_floor(&cur, 4000);   // the second eye of the frame: no step back
    CHECK(cur == 4010);
}
