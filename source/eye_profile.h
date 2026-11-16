#ifndef EYE_PROFILE_H
#define EYE_PROFILE_H

// Where an eye's layer pass spends its CPU time, measured on the console.
//
// The pass of one eye is about 2.2 ms of a drawn frame in the MMX3 ZP dam
// in Old 3DS mode (console log of 2026-11-16) and the perf line only gives
// its total. This profile splits it: the draw calls by phase (the layers,
// the shadow/glow phase, the layer scratch build), and inside a draw call
// the state side (render state and attribute buffers) apart from citro3d's
// (context update and command emission). The index loops of the tiled
// layers are kept apart as well.
//
// It reads the tick counter three times a draw call, so it runs on one
// drawn frame in EYE_PROFILE_EVERY: the other frames pay one test a call.
// Timers mean nothing in the emulators; the counts do.

#define EYE_PROFILE_EVERY 8

enum {
    EYE_PHASE_LAYERS = 0,   // everything that is not one of the two below
    EYE_PHASE_FX,           // drop shadows and glow
    EYE_PHASE_SCRATCH,      // deciding and building the layer scratch images (left eye only)
    EYE_PHASES
};

struct EyeProfileCell {
    unsigned int       draws;
    unsigned long long drawTicks;    // inside gpu3dsDraw
    unsigned long long stateTicks;   // ...of which render state and attribute buffers
    unsigned long long c3dTicks;     // ...of which the citro3d draw call
};

struct EyeProfile {
    bool               on;           // the frame being drawn is a sampled one
    unsigned char      eye;          // 0 left, 1 right
    unsigned char      phase;
    unsigned int       drawnFrames;  // every drawn frame, never reset
    unsigned int       samples[2];   // passes sampled in this window, by eye
    unsigned long long eyeTicks[2];  // the whole pass
    unsigned long long phaseTicks[2][EYE_PHASES];   // FX and SCRATCH; LAYERS is the rest
    unsigned long long indexTicks[2];               // the tiled layers' index loops, their draws left out
    unsigned int       cullTiles[2];                // tiles those loops tested one by one (occlusion and window cull)
    unsigned int       plainVerts[2];               // vertices they passed on untested
    EyeProfileCell     cell[2][EYE_PHASES];
};

// a drawn frame starts: decides whether it is sampled
static inline void eye_profile_begin_frame(EyeProfile *p)
{
    p->on = (p->drawnFrames++ % EYE_PROFILE_EVERY) == 0;
    p->eye = 0;
    p->phase = EYE_PHASE_LAYERS;
}

// one eye's pass ended, of a sampled frame
static inline void eye_profile_pass(EyeProfile *p, int eye, unsigned long long ticks)
{
    if (!p->on) return;
    p->samples[eye & 1]++;
    p->eyeTicks[eye & 1] += ticks;
}

// enters a phase; returns the one to give back to eye_profile_leave
static inline unsigned char eye_profile_enter(EyeProfile *p, unsigned char phase)
{
    const unsigned char prev = p->phase;
    p->phase = phase;
    return prev;
}

static inline void eye_profile_leave(EyeProfile *p, unsigned char prev, unsigned long long ticks)
{
    if (p->on) p->phaseTicks[p->eye & 1][p->phase] += ticks;
    p->phase = prev;
}

// one draw call: t0 at its start, t1 with the state applied, t2 at its end
static inline void eye_profile_draw(EyeProfile *p, unsigned long long t0, unsigned long long t1, unsigned long long t2)
{
    EyeProfileCell *c = &p->cell[p->eye & 1][p->phase];
    c->draws++;
    c->drawTicks += t2 - t0;
    c->stateTicks += t1 - t0;
    c->c3dTicks += t2 - t1;
}

// the time inside draw calls so far, for the pass and phase in course
static inline unsigned long long eye_profile_draw_ticks(const EyeProfile *p)
{
    return p->cell[p->eye & 1][p->phase].drawTicks;
}

// an index loop ran from t0 to t1 with drawTicks0 read at t0: its draw
// calls are in the cells already
static inline void eye_profile_index(EyeProfile *p, unsigned long long t0, unsigned long long t1, unsigned long long drawTicks0)
{
    p->indexTicks[p->eye & 1] += (t1 - t0) - (eye_profile_draw_ticks(p) - drawTicks0);
}

// what an index loop walked: tiles tested one by one, vertices copied as they are
static inline void eye_profile_walked(EyeProfile *p, unsigned int cullTiles, unsigned int plainVerts)
{
    p->cullTiles[p->eye & 1] += cullTiles;
    p->plainVerts[p->eye & 1] += plainVerts;
}

// the window was reported: the sums start again, the frame count goes on
static inline void eye_profile_window_reset(EyeProfile *p)
{
    for (int e = 0; e < 2; e++) {
        p->samples[e] = 0;
        p->eyeTicks[e] = 0;
        p->indexTicks[e] = 0;
        p->cullTiles[e] = p->plainVerts[e] = 0;
        for (int k = 0; k < EYE_PHASES; k++) {
            p->phaseTicks[e][k] = 0;
            p->cell[e][k].draws = 0;
            p->cell[e][k].drawTicks = p->cell[e][k].stateTicks = p->cell[e][k].c3dTicks = 0;
        }
    }
}

#endif
