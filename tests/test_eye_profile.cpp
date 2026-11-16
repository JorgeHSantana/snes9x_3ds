#include "doctest.h"
#include <string.h>
#include "eye_profile.h"

namespace {
EyeProfile fresh() { EyeProfile p; memset(&p, 0, sizeof(p)); return p; }
}

TEST_CASE("eye profile: one drawn frame in EYE_PROFILE_EVERY is sampled") {
    EyeProfile p = fresh();
    int sampled = 0;
    for (int f = 0; f < EYE_PROFILE_EVERY * 5; f++) {
        eye_profile_begin_frame(&p);
        if (p.on) sampled++;
        CHECK(p.on == (f % EYE_PROFILE_EVERY == 0));
    }
    CHECK(sampled == 5);
}

TEST_CASE("eye profile: a pass counts only in a sampled frame, each eye apart") {
    EyeProfile p = fresh();
    eye_profile_begin_frame(&p);          // sampled
    eye_profile_pass(&p, 0, 100);
    eye_profile_pass(&p, 1, 70);
    eye_profile_begin_frame(&p);          // not sampled
    eye_profile_pass(&p, 0, 999);
    CHECK(p.samples[0] == 1);
    CHECK(p.samples[1] == 1);
    CHECK(p.eyeTicks[0] == 100);
    CHECK(p.eyeTicks[1] == 70);
}

TEST_CASE("eye profile: a draw call lands in the cell of its eye and phase") {
    EyeProfile p = fresh();
    eye_profile_begin_frame(&p);
    eye_profile_draw(&p, 10, 14, 30);                       // left, layers
    const unsigned char prev = eye_profile_enter(&p, EYE_PHASE_FX);
    eye_profile_draw(&p, 40, 41, 50);                       // left, fx
    eye_profile_leave(&p, prev, 25);
    p.eye = 1;
    eye_profile_draw(&p, 60, 63, 70);                       // right, layers again
    const EyeProfileCell &l = p.cell[0][EYE_PHASE_LAYERS], &fx = p.cell[0][EYE_PHASE_FX], &r = p.cell[1][EYE_PHASE_LAYERS];
    CHECK(l.draws == 1); CHECK(l.drawTicks == 20); CHECK(l.stateTicks == 4); CHECK(l.c3dTicks == 16);
    CHECK(fx.draws == 1); CHECK(fx.drawTicks == 10); CHECK(fx.stateTicks == 1); CHECK(fx.c3dTicks == 9);
    CHECK(r.draws == 1); CHECK(r.drawTicks == 10);
    CHECK(p.phaseTicks[0][EYE_PHASE_FX] == 25);
    CHECK(p.phase == EYE_PHASE_LAYERS);
}

TEST_CASE("eye profile: a phase inside a phase gives the outer one back") {
    EyeProfile p = fresh();
    eye_profile_begin_frame(&p);
    const unsigned char outer = eye_profile_enter(&p, EYE_PHASE_SCRATCH);
    const unsigned char inner = eye_profile_enter(&p, EYE_PHASE_FX);
    eye_profile_leave(&p, inner, 5);
    CHECK(p.phase == EYE_PHASE_SCRATCH);
    eye_profile_leave(&p, outer, 9);
    CHECK(p.phase == EYE_PHASE_LAYERS);
    CHECK(p.phaseTicks[0][EYE_PHASE_FX] == 5);
    CHECK(p.phaseTicks[0][EYE_PHASE_SCRATCH] == 9);
}

TEST_CASE("eye profile: a phase of a frame that is not sampled adds no time") {
    EyeProfile p = fresh();
    eye_profile_begin_frame(&p);
    eye_profile_begin_frame(&p);          // not sampled
    const unsigned char prev = eye_profile_enter(&p, EYE_PHASE_FX);
    eye_profile_leave(&p, prev, 50);
    CHECK(p.phaseTicks[0][EYE_PHASE_FX] == 0);
    CHECK(p.phase == EYE_PHASE_LAYERS);
}

TEST_CASE("eye profile: an index loop's time leaves its draw calls out") {
    EyeProfile p = fresh();
    eye_profile_begin_frame(&p);
    eye_profile_draw(&p, 0, 1, 5);                          // before the loop: not its business
    const unsigned long long d0 = eye_profile_draw_ticks(&p);
    eye_profile_draw(&p, 110, 112, 130);                    // two draws inside a loop that ran 100..200
    eye_profile_draw(&p, 150, 151, 160);
    eye_profile_index(&p, 100, 200, d0);
    CHECK(p.indexTicks[0] == 100 - 20 - 10);
    CHECK(p.indexTicks[1] == 0);
}

TEST_CASE("eye profile: what the index loops walked is counted by eye") {
    EyeProfile p = fresh();
    eye_profile_begin_frame(&p);
    eye_profile_walked(&p, 32, 0);
    eye_profile_walked(&p, 0, 128);
    p.eye = 1;
    eye_profile_walked(&p, 7, 2);
    CHECK(p.cullTiles[0] == 32); CHECK(p.plainVerts[0] == 128);
    CHECK(p.cullTiles[1] == 7);  CHECK(p.plainVerts[1] == 2);
    eye_profile_window_reset(&p);
    CHECK(p.cullTiles[0] == 0); CHECK(p.plainVerts[1] == 0);
}

TEST_CASE("eye profile: the window reset clears the sums and keeps the frame count") {
    EyeProfile p = fresh();
    for (int f = 0; f < 3; f++) eye_profile_begin_frame(&p);
    p.on = true;
    eye_profile_pass(&p, 0, 10);
    eye_profile_draw(&p, 0, 1, 2);
    eye_profile_index(&p, 0, 9, 0);
    eye_profile_window_reset(&p);
    CHECK(p.samples[0] == 0);
    CHECK(p.eyeTicks[0] == 0);
    CHECK(p.indexTicks[0] == 0);
    CHECK(p.cell[0][EYE_PHASE_LAYERS].draws == 0);
    CHECK(p.cell[0][EYE_PHASE_LAYERS].drawTicks == 0);
    CHECK(p.drawnFrames == 3);
    // the next sampled frame is still the one the count says
    int until = 0;
    do { eye_profile_begin_frame(&p); until++; } while (!p.on);
    CHECK(until == EYE_PROFILE_EVERY - 3 + 1);
}
