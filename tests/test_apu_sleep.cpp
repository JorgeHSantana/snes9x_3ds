#include "doctest.h"
#include "apu_sleep.h"

static const uint8_t g_ram[16] = {0};

static SApuSleepSig sigAt(int pc)
{
    SApuSleepSig s = {};
    s.pc = g_ram + pc;
    s.directPage = g_ram;
    s.ya = 0x1234; s.x = 5; s.s = 0xCF; s.p = 0x02;
    return s;
}

TEST_CASE("apu_sleep_visit: the same state twice with nothing dirty proves the period") {
    SApuSleepDetector d = {};
    d.dirty = 1;                                   // whatever ran before the loop
    SApuSleepSig a = sigAt(2);
    CHECK(apu_sleep_visit(&d, &a, 1000) == 0);     // dirty: only clears
    CHECK(apu_sleep_visit(&d, &a, 1840) == 0);     // clean: anchor
    CHECK(apu_sleep_visit(&d, &a, 2680) == 840);   // back in the same state
    CHECK_FALSE(d.have);                           // the caller starts over on wake
}

TEST_CASE("apu_sleep_visit: a change between visits restarts the proof") {
    SApuSleepDetector d = {};
    SApuSleepSig a = sigAt(2);
    CHECK(apu_sleep_visit(&d, &a, 0) == 0);        // anchor
    d.dirty = 1;                                   // a RAM byte changed
    CHECK(apu_sleep_visit(&d, &a, 840) == 0);
    CHECK(apu_sleep_visit(&d, &a, 1680) == 0);     // anchor again
    CHECK(apu_sleep_visit(&d, &a, 2520) == 840);
}

TEST_CASE("apu_sleep_visit: any register difference is another state") {
    SApuSleepSig a = sigAt(2);
    SApuSleepSig v[] = {a, a, a, a, a, a, a, a, a, a, a, a};
    v[0].pc = g_ram + 3;   v[1].ya = 0x1235;       v[2].x = 6;          v[3].s = 0xCE;
    v[4].p = 0x22;         v[5].carry = 1;         v[6].zero = 0x80;    v[7].overflow = 1;
    v[8].directPage = g_ram + 1; v[9].wait1 = g_ram + 4; v[10].wait2 = g_ram + 5; v[11].waitCounter = 1;
    for (int i = 0; i < 12; i++)
    {
        SApuSleepDetector d = {};
        CHECK(apu_sleep_visit(&d, &a, 0) == 0);
        CHECK(apu_sleep_visit(&d, &v[i], 840) == 0);
    }
}

TEST_CASE("apu_sleep_visit: a loop with two taken branches is proven at the anchored one") {
    SApuSleepDetector d = {};
    SApuSleepSig a = sigAt(2), b = sigAt(9);
    CHECK(apu_sleep_visit(&d, &a, 0) == 0);
    CHECK(apu_sleep_visit(&d, &b, 300) == 0);
    CHECK(apu_sleep_visit(&d, &a, 840) == 840);
}

TEST_CASE("apu_sleep_visit: an anchor the code left is replaced after APU_SLEEP_REANCHOR visits") {
    SApuSleepDetector d = {};
    SApuSleepSig gone = sigAt(1), a = sigAt(2);
    CHECK(apu_sleep_visit(&d, &gone, 0) == 0);
    for (int i = 1; i < APU_SLEEP_REANCHOR; i++)
        CHECK(apu_sleep_visit(&d, &a, i * 840) == 0);
    CHECK(d.anchor.pc == gone.pc);
    CHECK(apu_sleep_visit(&d, &a, APU_SLEEP_REANCHOR * 840) == 0);       // moves here
    CHECK(d.anchor.pc == a.pc);
    CHECK(apu_sleep_visit(&d, &a, (APU_SLEEP_REANCHOR + 1) * 840) == 840);
}

TEST_CASE("apu_sleep_visit: a period that is not positive proves nothing") {
    SApuSleepDetector d = {};
    SApuSleepSig a = sigAt(2);
    CHECK(apu_sleep_visit(&d, &a, 500) == 0);
    CHECK(apu_sleep_visit(&d, &a, 500) == 0);      // re-anchors
    CHECK(apu_sleep_visit(&d, &a, 400) == 0);
    CHECK(apu_sleep_visit(&d, &a, 1240) == 840);
}

TEST_CASE("apu_sleep_forward: whole periods, never past the limit") {
    CHECK(apu_sleep_forward(100, 100, 840) == 100);
    CHECK(apu_sleep_forward(100, 939, 840) == 100);
    CHECK(apu_sleep_forward(100, 940, 840) == 940);      // lands on the limit: still runs
    CHECK(apu_sleep_forward(100, 5000, 840) == 4300);
    CHECK(apu_sleep_forward(-3000, 100, 840) == -480);   // rebased below zero
    CHECK(apu_sleep_forward(100, 50, 840) == 100);       // already past the limit
    CHECK(apu_sleep_forward(100, 5000, 0) == 100);
}

TEST_CASE("apu_sleep_forward: equals stepping one period at a time") {
    for (int limit = -2000; limit < 4000; limit += 37)
    {
        int32_t c = -1500;
        while (c + 840 <= limit) c += 840;
        CHECK(apu_sleep_forward(-1500, limit, 840) == c);
    }
}

TEST_CASE("apu_sleep_timer_will_tick: mirrors S9xIncrementAPUTimers") {
    uint8_t  en[3]     = {1, 1, 1};
    uint16_t timer[3]  = {0, 0, 0};
    uint16_t target[3] = {4, 4, 4};
    CHECK_FALSE(apu_sleep_timer_will_tick(0, en, timer, target));
    timer[2] = 3;                                          // timer 2 steps every call
    CHECK(apu_sleep_timer_will_tick(0, en, timer, target));
    en[2] = 0;
    CHECK_FALSE(apu_sleep_timer_will_tick(0, en, timer, target));
    timer[0] = 3;                                          // timers 0 and 1 only on the eighth
    CHECK_FALSE(apu_sleep_timer_will_tick(6, en, timer, target));
    CHECK(apu_sleep_timer_will_tick(7, en, timer, target));
    en[0] = 0;
    CHECK_FALSE(apu_sleep_timer_will_tick(7, en, timer, target));
    timer[1] = 3;
    CHECK(apu_sleep_timer_will_tick(7, en, timer, target));
    target[1] = 0x100; timer[1] = 0xFE;                    // target 0 is stored as 0x100
    CHECK_FALSE(apu_sleep_timer_will_tick(7, en, timer, target));
    timer[1] = 0xFF;
    CHECK(apu_sleep_timer_will_tick(7, en, timer, target));
}
