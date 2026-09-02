#include "doctest.h"
#include "../source/3dsblurauto.h"

// one window of frames, the first `skips` of them skipped
static bool runWindow(BlurAutoState *s, int skips)
{
    bool v = false;
    for (int i = 0; i < BLUR_AUTO_WINDOW_FRAMES; i++)
        v = blurAutoStep(s, i < skips);
    return v;
}

// clean windows until Full returns; returns how many it took (capped)
static int windowsToFull(BlurAutoState *s)
{
    for (int w = 1; w <= BLUR_AUTO_CLEAN_MAX + 1; w++)
        if (runWindow(s, 0) == false) return w;
    return -1;
}

TEST_CASE("blur auto: null state is Full") {
    CHECK(blurAutoStep(nullptr, true) == false);
}

TEST_CASE("blur auto: clean frames stay Full") {
    BlurAutoState s; blurAutoReset(&s);
    for (int w = 0; w < 50; w++)
        CHECK(runWindow(&s, 0) == false);
}

TEST_CASE("blur auto: one skipped frame flips to Light immediately") {
    BlurAutoState s; blurAutoReset(&s);
    for (int i = 0; i < 17; i++) CHECK(blurAutoStep(&s, false) == false);
    CHECK(blurAutoStep(&s, true) == true);
    CHECK(blurAutoStep(&s, false) == true);
}

TEST_CASE("blur auto: Full returns after the base run of clean windows") {
    BlurAutoState s; blurAutoReset(&s);
    blurAutoStep(&s, true);
    CHECK(windowsToFull(&s) == BLUR_AUTO_CLEAN_BASE);
}

TEST_CASE("blur auto: a skip while Light restarts the clean run") {
    BlurAutoState s; blurAutoReset(&s);
    blurAutoStep(&s, true);
    CHECK(runWindow(&s, 0) == true);
    CHECK(runWindow(&s, 0) == true);
    CHECK(runWindow(&s, 1) == true);     // restart
    CHECK(windowsToFull(&s) == BLUR_AUTO_CLEAN_BASE);
}

TEST_CASE("blur auto: a quick relapse doubles the proof Full needs, up to the cap") {
    BlurAutoState s; blurAutoReset(&s);
    blurAutoStep(&s, true);
    CHECK(windowsToFull(&s) == 3);
    runWindow(&s, 0);                     // 1 window in Full, then a skip: relapse
    blurAutoStep(&s, true);
    CHECK(windowsToFull(&s) == 6);
    blurAutoStep(&s, true);               // relapse right away
    CHECK(windowsToFull(&s) == 12);
    blurAutoStep(&s, true); CHECK(windowsToFull(&s) == 24);
    blurAutoStep(&s, true); CHECK(windowsToFull(&s) == 48);
    blurAutoStep(&s, true); CHECK(windowsToFull(&s) == BLUR_AUTO_CLEAN_MAX);
    blurAutoStep(&s, true); CHECK(windowsToFull(&s) == BLUR_AUTO_CLEAN_MAX);
}

TEST_CASE("blur auto: a long stable Full resets the proof to base") {
    BlurAutoState s; blurAutoReset(&s);
    blurAutoStep(&s, true); windowsToFull(&s);
    blurAutoStep(&s, true); CHECK(windowsToFull(&s) == 6);      // escalated
    for (int w = 0; w < BLUR_AUTO_STABLE_WINDOWS; w++) CHECK(runWindow(&s, 0) == false);
    blurAutoStep(&s, true);
    CHECK(windowsToFull(&s) == BLUR_AUTO_CLEAN_BASE);
}

TEST_CASE("blur auto: a relapse between the relapse and stable marks keeps the current proof") {
    BlurAutoState s; blurAutoReset(&s);
    blurAutoStep(&s, true); windowsToFull(&s);
    blurAutoStep(&s, true); CHECK(windowsToFull(&s) == 6);
    for (int w = 0; w < BLUR_AUTO_RELAPSE_WINDOWS + 5; w++) runWindow(&s, 0);
    blurAutoStep(&s, true);
    CHECK(windowsToFull(&s) == 6);
}

TEST_CASE("blur auto: reset clears everything") {
    BlurAutoState s; blurAutoReset(&s);
    blurAutoStep(&s, true); windowsToFull(&s); blurAutoStep(&s, true);
    blurAutoReset(&s);
    CHECK(s.light == false); CHECK(s.required == BLUR_AUTO_CLEAN_BASE);
    CHECK(s.clean == 0); CHECK(s.frames == 0); CHECK(s.skips == 0);
}
