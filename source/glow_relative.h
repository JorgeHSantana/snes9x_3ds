#ifndef GLOW_RELATIVE_H
#define GLOW_RELATIVE_H

#include <stdint.h>
#include "stereo_glow.h"

// Glow Threshold "Relative" (issue #77, Jorge: "the glow disappears in
// dark scenes, where it should look best"). The absolute cut (Threshold x
// 16 on a 0..255 luma) does not know the scene is dark. Relative reads the
// SNES palette (CGRAM, 256 BGR555 entries, CPU, ~nothing) every frame,
// takes its 90th-percentile luma as "the scene's bright", and the cut
// becomes Threshold/16 of THAT; the gain scales up by 255/bright (at most
// 2x) so a weak light adds like a light. The bright value is tracked over
// GLOW_REL_TRACK_FRAMES so a blinking colour cannot make the cut flicker.

#define GLOW_REL_TRACK_FRAMES 20
#define GLOW_REL_MIN_BRIGHT   32     // below this the scene is black: do not chase noise

// luma 0..255 of a BGR555 entry (same weights as the TexEnv: 76 R, 150 G, 29 B)
static inline int glow_rel_luma(uint16_t bgr555)
{
    const int r = (bgr555 & 31) << 3, g = ((bgr555 >> 5) & 31) << 3, b = ((bgr555 >> 10) & 31) << 3;
    return (r * 76 + g * 150 + b * 29) / 255;
}

// the 90th-percentile luma of n palette entries (0..255)
static inline int glow_rel_bright(const uint16_t *cg, int n)
{
    int hist[32] = { 0 };
    for (int i = 0; i < n; i++) hist[glow_rel_luma(cg[i]) >> 3]++;
    const int want = (n * 9 + 9) / 10;   // entries at or below the answer
    int acc = 0;
    for (int b = 0; b < 32; b++) {
        acc += hist[b];
        if (acc >= want) return b * 8 + 7;
    }
    return 255;
}

struct GlowRelState { float bright; };   // tracked scene bright, 0..255

static inline void glowRelReset(GlowRelState *s) { s->bright = 255.0f; }

static inline void glowRelStep(GlowRelState *s, int brightNow)
{
    const float target = (float)(brightNow < GLOW_REL_MIN_BRIGHT ? GLOW_REL_MIN_BRIGHT : brightNow);
    s->bright += (target - s->bright) / (float)GLOW_REL_TRACK_FRAMES;
}

// the alpha-test reference for this frame: Threshold/16 of the scene's bright
static inline int glow_rel_ref(int thresh, float bright)
{
    const int r = (int)((float)stereo_glow_clamp_thresh(thresh) * bright / 16.0f + 0.5f);
    return r < 1 ? 1 : (r > 255 ? 255 : r);
}

// the gain multiplier: 255/bright, capped at 2
static inline float glow_rel_gain(float bright)
{
    const float m = bright < 1.0f ? 2.0f : 255.0f / bright;
    return m > 2.0f ? 2.0f : (m < 1.0f ? 1.0f : m);
}

#endif
