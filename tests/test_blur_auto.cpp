#include "doctest.h"
#include "../source/3dsblurauto.h"

static bool runWindow(BlurAutoState *s, int skips)
{
    bool v = false;
    for (int i = 0; i < BLUR_AUTO_WINDOW_FRAMES; i++)
        v = blurAutoStep(s, i < skips);
    return v;
}

TEST_CASE("blur auto: null state is Full") {
    CHECK(blurAutoStep(nullptr, true) == false);
}

TEST_CASE("blur auto: clean windows stay Full") {
    BlurAutoState s; blurAutoReset(&s);
    for (int w = 0; w < 10; w++)
        CHECK(runWindow(&s, 0) == false);
}

TEST_CASE("blur auto: one skip per window is tolerated") {
    BlurAutoState s; blurAutoReset(&s);
    for (int w = 0; w < 10; w++)
        CHECK(runWindow(&s, 1) == false);
}

TEST_CASE("blur auto: trigger skips switch to Light at the window edge, not before") {
    BlurAutoState s; blurAutoReset(&s);
    for (int i = 0; i < BLUR_AUTO_WINDOW_FRAMES - 1; i++)
        CHECK(blurAutoStep(&s, i < BLUR_AUTO_SKIP_TRIGGER) == false);
    CHECK(blurAutoStep(&s, false) == true);
}

TEST_CASE("blur auto: Light persists through the cooldown and returns to Full after clean windows") {
    BlurAutoState s; blurAutoReset(&s);
    CHECK(runWindow(&s, BLUR_AUTO_SKIP_TRIGGER) == true);
    for (int w = 0; w < BLUR_AUTO_CLEAN_WINDOWS - 1; w++)
        CHECK(runWindow(&s, 0) == true);
    CHECK(runWindow(&s, 0) == false);
}

TEST_CASE("blur auto: a skip during cooldown re-arms it") {
    BlurAutoState s; blurAutoReset(&s);
    CHECK(runWindow(&s, BLUR_AUTO_SKIP_TRIGGER) == true);
    CHECK(runWindow(&s, 0) == true);
    CHECK(runWindow(&s, 1) == true);     // re-armed
    CHECK(runWindow(&s, 0) == true);
    CHECK(runWindow(&s, 0) == true);
    CHECK(runWindow(&s, 0) == false);    // three clean windows since the re-arm
}

TEST_CASE("blur auto: reset clears everything") {
    BlurAutoState s; blurAutoReset(&s);
    runWindow(&s, BLUR_AUTO_SKIP_TRIGGER);
    blurAutoReset(&s);
    CHECK(s.light == false); CHECK(s.cooldown == 0); CHECK(s.frames == 0); CHECK(s.skips == 0);
}
