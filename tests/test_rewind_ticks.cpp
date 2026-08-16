// Host-side tests for the rewind v2 window tick pool (degrau 3).

#include "doctest.h"

#include "3dsrewindticks.h"

namespace {

struct TicksFixture
{
    static constexpr int SLOTS = 4;
    uint32_t frames[SLOTS];
    RewindTicks ticks;

    TicksFixture() { ticks.init(frames, SLOTS); }
};

} // namespace

TEST_CASE("ticks: alloc fills free slots then evicts farthest from cursor")
{
    TicksFixture f;
    CHECK(f.ticks.alloc(100, 100) >= 0);
    CHECK(f.ticks.alloc(160, 160) >= 0);
    CHECK(f.ticks.alloc(220, 220) >= 0);
    CHECK(f.ticks.alloc(280, 280) >= 0);

    // cursor at 280: frame 100 is farthest and gets evicted
    int slot = f.ticks.alloc(340, 280);
    CHECK(slot >= 0);
    CHECK(f.ticks.find(100) < 0);
    CHECK(f.ticks.find(340) >= 0);

    // a newcomer farther than every resident is refused
    CHECK(f.ticks.alloc(1000, 280) < 0);
}

TEST_CASE("ticks: best_source is the closest state at or below")
{
    TicksFixture f;
    f.ticks.alloc(100, 100);
    f.ticks.alloc(220, 220);

    int slot = f.ticks.best_source(219);
    REQUIRE(slot >= 0);
    CHECK(f.ticks.frames[slot] == 100);
    slot = f.ticks.best_source(220);
    REQUIRE(slot >= 0);
    CHECK(f.ticks.frames[slot] == 220);
    CHECK(f.ticks.best_source(99) < 0);
}

TEST_CASE("ticks: next_missing walks outward from the cursor")
{
    TicksFixture f;
    // cursor tick present; neighbours missing
    f.ticks.alloc(300, 300);
    CHECK(f.ticks.next_missing(300, 60, 2, 180, 420) == 240);   // -1 tick first
    f.ticks.alloc(240, 300);
    CHECK(f.ticks.next_missing(300, 60, 2, 180, 420) == 360);   // then +1
    f.ticks.alloc(360, 300);
    CHECK(f.ticks.next_missing(300, 60, 2, 180, 420) == 180);   // then -2
    f.ticks.alloc(180, 300);
    CHECK(f.ticks.next_missing(300, 60, 2, 180, 420) == 420);   // then +2
    // pool smaller than the neighbourhood: the newcomer ties the farthest
    // resident's distance and is refused (production sizes the pool larger
    // than the prefetch radius, so this only happens at the edges)
    CHECK(f.ticks.alloc(420, 300) < 0);
    CHECK(f.ticks.next_missing(300, 60, 2, 180, 420) == 420);

    // bounds clamp the walk
    CHECK(f.ticks.next_missing(300, 60, 2, 290, 310) == RewindTicks::NO_FRAME);
}

TEST_CASE("ticks: drop_after clears the abandoned future")
{
    TicksFixture f;
    f.ticks.alloc(100, 100);
    f.ticks.alloc(160, 160);
    f.ticks.alloc(220, 220);
    f.ticks.drop_after(160);
    CHECK(f.ticks.find(100) >= 0);
    CHECK(f.ticks.find(160) >= 0);
    CHECK(f.ticks.find(220) < 0);
}
