#ifndef _3DSMODE7PERSP_H_
#define _3DSMODE7PERSP_H_

#include <stdint.h>

// Mode 7 perspective for stereo 3D (issue #62, rcmz's technique made
// ours). Every Mode 7 scanline already knows how many texels it walks
// across the screen (the A/B/C/D matrix). That span IS the row's distance:
// the nearest row (screen bottom in a race) walks few texels per pixel,
// the horizon walks many. The stereo shift of a row scales with the
// nearest row's span over its own, so the ground recedes instead of
// standing up like a wall - and a top-down map (uniform span) stays flat
// on its own, nothing to detect.
//
// The factor rides in the scanline vertex's w as 256 * (spanRef / span),
// clamped to [MODE7_PERSP_W_MIN, MODE7_PERSP_W_ONE]. The tile vertex
// shader applies shift *= 1 + k * (w/256 - 1) with k the profile's
// strength (0..1) for the Mode 7 layer draw and 0 for every other draw,
// so tiles and 2D are exact no-ops.

#define MODE7_PERSP_W_ONE 256
#define MODE7_PERSP_W_MIN 8

static inline int16_t mode7PerspEncode(float span, float spanRef)
{
    if (!(span > 0.0f) || !(spanRef > 0.0f))
        return MODE7_PERSP_W_ONE;
    float f = spanRef / span;
    if (f > 1.0f) f = 1.0f;
    int w = (int)(f * (float)MODE7_PERSP_W_ONE + 0.5f);
    if (w < MODE7_PERSP_W_MIN) w = MODE7_PERSP_W_MIN;
    if (w > MODE7_PERSP_W_ONE) w = MODE7_PERSP_W_ONE;
    return (int16_t)w;
}

// the shader's formula, mirrored for tests
static inline float mode7PerspFactor(int16_t w, float k)
{
    return 1.0f + k * ((float)w / (float)MODE7_PERSP_W_ONE - 1.0f);
}

#endif
