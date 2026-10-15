#ifndef SHADOW_AUTO_H
#define SHADOW_AUTO_H

// Shadow Quality (issue #77, Jorge): the drop shadow follows the same
// load verdict Blur Auto computes (3dsblurauto.h: 0 Full, 1 Light, 2 Off),
// read whatever Blur Quality is set to. Pure logic, one step per frame.
//
// mode 0 "Full":                 no optimisation.
// mode 1 "Auto":                 the Soft Edge level drops one step per
//                                load tier (2 -> 1 -> 0) and climbs back
//                                with the controller's hysteresis; the
//                                shadow itself never goes.
// mode 2 "Auto, Off under load": mode 1, and at the Off tier the shadow
//                                fades out over SHADOW_AUTO_FADE_FRAMES
//                                (and back in the same way) instead of
//                                vanishing between two frames.

#define SHADOW_AUTO_FADE_FRAMES 30

struct ShadowAutoState
{
    int   level;      // effective Soft Edge level this frame
    float alphaMul;   // 0..1 multiplier on the shadow's alpha (the fade)
};

static inline void shadowAutoReset(ShadowAutoState *s, int softLevel)
{
    s->level = softLevel;
    s->alphaMul = 1.0f;
}

static inline void shadowAutoStep(ShadowAutoState *s, int mode, int loadTier, int softLevel)
{
    int level = softLevel;
    float target = 1.0f;
    if (mode >= 1) {
        level = softLevel - (loadTier < 0 ? 0 : loadTier);
        if (level < 0) level = 0;
    }
    if (mode >= 2 && loadTier >= 2) {
        level = 0;
        target = 0.0f;
    }
    s->level = level;
    const float step = 1.0f / SHADOW_AUTO_FADE_FRAMES;
    if (s->alphaMul < target) { s->alphaMul += step; if (s->alphaMul > target) s->alphaMul = target; }
    else if (s->alphaMul > target) { s->alphaMul -= step; if (s->alphaMul < target) s->alphaMul = target; }
}

#endif
