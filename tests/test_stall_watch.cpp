#include "doctest.h"
#include "stall_watch.h"

TEST_CASE("stall watch: a counter that moves never reports")
{
    StallWatch w = {0, 0, 0};
    for (uint32_t i = 1; i <= 100; i++)
        CHECK(stall_watch_step(&w, true, i, (uint64_t)i * 250) == STALL_NONE);
}

TEST_CASE("stall watch: began once at the threshold, one follow-up, then quiet")
{
    StallWatch w = {7, 1000, 0};
    CHECK(stall_watch_step(&w, true, 7, 1000 + STALL_THRESHOLD_MS - 1) == STALL_NONE);
    CHECK(stall_watch_step(&w, true, 7, 1000 + STALL_THRESHOLD_MS) == STALL_BEGAN);
    CHECK(stall_watch_step(&w, true, 7, 1000 + STALL_THRESHOLD_MS + 250) == STALL_NONE);
    CHECK(stall_watch_step(&w, true, 7, 1000 + STALL_THRESHOLD_MS + STALL_FOLLOWUP_MS) == STALL_STILL);
    CHECK(stall_watch_step(&w, true, 7, 1000 + 60000) == STALL_NONE);
}

TEST_CASE("stall watch: the counter moving again ends a reported stall and re-arms")
{
    StallWatch w = {7, 0, 0};
    CHECK(stall_watch_step(&w, true, 7, STALL_THRESHOLD_MS) == STALL_BEGAN);
    CHECK(stall_watch_step(&w, true, 8, STALL_THRESHOLD_MS + 500) == STALL_ENDED);
    CHECK(stall_watch_step(&w, true, 8, STALL_THRESHOLD_MS + 600) == STALL_NONE);
    CHECK(stall_watch_step(&w, true, 8, 2 * STALL_THRESHOLD_MS + 500) == STALL_BEGAN);
}

TEST_CASE("stall watch: disarmed time is not a stall and does not count towards one")
{
    StallWatch w = {7, 0, 0};
    CHECK(stall_watch_step(&w, false, 7, 10000) == STALL_NONE);
    CHECK(stall_watch_step(&w, true, 7, 10000 + STALL_THRESHOLD_MS - 1) == STALL_NONE);
    CHECK(stall_watch_step(&w, true, 7, 10000 + STALL_THRESHOLD_MS) == STALL_BEGAN);
    CHECK(stall_watch_step(&w, false, 7, 10000 + STALL_THRESHOLD_MS + 10) == STALL_ENDED);
}
