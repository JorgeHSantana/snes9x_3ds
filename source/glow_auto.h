#ifndef GLOW_AUTO_H
#define GLOW_AUTO_H

// Glow Quality (issue #77, Jorge: "make the Halo automatic if it gets
// heavy, and keep the changes smooth"). Pure logic, one step per frame,
// fed the same load tier Blur Auto computes (0 Full, 1 Light, 2 Off).
//
// mode 0 "Full": no optimisation, drop 0, gain 1.
// mode 1 "Auto": the Halo and the Spread drop one level per load tier
//                (the caller subtracts `drop`); a level change never pops:
//                the glow's gain fades to 0 over GLOW_AUTO_FADE_FRAMES,
//                the level switches at 0, the gain fades back to 1.

#define GLOW_AUTO_FADE_FRAMES 20

struct GlowAutoState
{
    int   drop;     // levels taken off the Halo/Spread this frame
    int   target;   // the drop the load asks for
    float gainMul;  // 0..1 multiplier on the glow's gain (the crossfade)
};

static inline void glowAutoReset(GlowAutoState *s)
{
    s->drop = 0; s->target = 0; s->gainMul = 1.0f;
}

static inline void glowAutoStep(GlowAutoState *s, int mode, int loadTier)
{
    s->target = mode >= 1 ? (loadTier < 0 ? 0 : (loadTier > 2 ? 2 : loadTier)) : 0;
    const float step = 1.0f / GLOW_AUTO_FADE_FRAMES;
    if (s->drop != s->target) {
        // on the way out: fade, then switch at zero
        s->gainMul -= step;
        if (s->gainMul <= 0.0f) { s->gainMul = 0.0f; s->drop = s->target; }
        return;
    }
    if (s->gainMul < 1.0f) { s->gainMul += step; if (s->gainMul > 1.0f) s->gainMul = 1.0f; }
}

#endif
