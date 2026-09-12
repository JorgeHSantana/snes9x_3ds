#include "doctest.h"
#include "perf_stats.h"

#include <limits>

TEST_CASE("timing stats aggregate count average and maximum without allocation")
{
    TimingStats stats;
    stats.record(10);
    stats.record(30);
    stats.record(20);

    CHECK(stats.sample_count() == 3);
    CHECK(stats.average_ticks() == 20);
    CHECK(stats.maximum_ticks() == 30);
}

TEST_CASE("timing stats reset to an empty safe state")
{
    TimingStats stats;
    stats.record(50);
    stats.reset();

    CHECK(stats.sample_count() == 0);
    CHECK(stats.average_ticks() == 0);
    CHECK(stats.maximum_ticks() == 0);
}

TEST_CASE("timing stats saturate instead of wrapping")
{
    TimingStats stats;
    stats.total_ticks = std::numeric_limits<uint64_t>::max() - 2;
    stats.samples = std::numeric_limits<uint32_t>::max() - 1;
    stats.record(5);
    stats.record(9);

    CHECK(stats.total_ticks == std::numeric_limits<uint64_t>::max());
    CHECK(stats.sample_count() == std::numeric_limits<uint32_t>::max());
    CHECK(stats.maximum_ticks() == 9);
}

TEST_CASE("tick conversion is exact across whole seconds without overflow")
{
    constexpr uint64_t clock_hz = 268123480;
    CHECK(ticks_to_microseconds(0, clock_hz) == 0);
    CHECK(ticks_to_microseconds(clock_hz, clock_hz) == 1000000);
    CHECK(ticks_to_microseconds(clock_hz + clock_hz / 2, clock_hz) == 1500000);
    CHECK(ticks_to_microseconds(clock_hz, 0) == 0);
}
