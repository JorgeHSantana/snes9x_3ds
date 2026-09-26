#include "doctest.h"
#include "sig_throttle.h"

TEST_CASE("sig throttle: distinct changes are logged, a toggle is logged once per value") {
    SigThrottle t; t.reset();
    CHECK(t.admit(1, 0));
    CHECK(t.admit(2, 0));
    CHECK_FALSE(t.admit(1, 0));          // A/B/A: A already in the history
    CHECK_FALSE(t.admit(2, 0));
    CHECK(t.admit(3, 0));                // a new scene still logs
    CHECK(t.dropped == 2);
}

TEST_CASE("sig throttle: at most 8 lines per window, the rest counted") {
    SigThrottle t; t.reset();
    int logged = 0;
    for (uint64_t i = 0; i < 20; i++) if (t.admit(100 + i, 0)) logged++;
    CHECK(logged == 8);
    CHECK(t.dropped == 12);
}

TEST_CASE("sig throttle: the summary comes at the window end, then waits 10 windows") {
    SigThrottle t; t.reset();
    t.admit(1, 0); t.admit(2, 0);
    for (int f = 0; f < 59; f++) { t.admit(1, 0); t.admit(2, 0); CHECK(t.tick() == 0); }
    CHECK(t.tick() == 118);              // frame 60: the window closes with the count
    // the next windows: toggling continues, no summary until the cooldown passes
    for (int w = 0; w < 10; w++) {
        for (int f = 0; f < 60; f++) { t.admit(1, 0); uint32_t n = t.tick(); if (w < 9 || f < 59) CHECK(n == 0); else CHECK(n == 600); }
    }
}

TEST_CASE("sig throttle: a quiet window writes nothing") {
    SigThrottle t; t.reset();
    for (int f = 0; f < 120; f++) CHECK(t.tick() == 0);
}
