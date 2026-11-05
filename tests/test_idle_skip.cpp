#include "doctest.h"
#include "idle_skip.h"

TEST_CASE("idle_skip_hit: the read is the loop's first instruction") {
    // T: LDA $4212 (3 bytes) ; BPL T  -> mark = T + 3
    CHECK(idle_skip_hit(0x8000, 0x8003));
    // T: LDA $004212 (4 bytes)
    CHECK(idle_skip_hit(0x8000, 0x8004));
    // T: LDA $xx (direct, 2 bytes) ; BEQ T
    CHECK(idle_skip_hit(0x8000, 0x8002));
}

TEST_CASE("idle_skip_hit: no mark, forward target or a loop with work before the read") {
    CHECK_FALSE(idle_skip_hit(0x8000, 0));          // nothing recorded
    CHECK_FALSE(idle_skip_hit(0x8003, 0x8003));     // target == mark (nothing in between)
    CHECK_FALSE(idle_skip_hit(0x8010, 0x8003));     // forward branch
    CHECK_FALSE(idle_skip_hit(0x8000, 0x8005));     // 5 bytes: an op before the read
    CHECK_FALSE(idle_skip_hit(0x8000, 0x9000));     // unrelated stale mark
}
