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
// shader applies shift += H * (1 - w/256) with H the eye's shift for the
// horizon's EXTRA depth (the layer gauge is the nearest row; the horizon
// sinks `recede` units past it, whatever the gauge's sign - the old
// multiplicative ramp flipped the plane with a negative gauge). H is set
// only for the Mode 7 layer draw and 0 for every other draw, so tiles and
// 2D are exact no-ops.

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

// the shader's formula, mirrored for tests: a row's shift from the
// nearest row's (the layer gauge) and the horizon's extra shift
static inline float mode7PerspRowShift(int16_t w, float nearShift, float horizonShift)
{
    return nearShift + horizonShift * (1.0f - (float)w / (float)MODE7_PERSP_W_ONE);
}

// Gauge 0..8 -> depth units the horizon sinks past the layer gauge
// (0 flat; 4 = a race track's natural recession; 8 = twice that).
static inline float mode7PerspRecede(int gauge)
{
    if (gauge < 0) gauge = 0;
    if (gauge > 8) gauge = 8;
    return (float)gauge;
}

// Effects by distance on a Mode 7 plane: the fog a row receives is
// amount * (1 - w/256) - nothing at the nearest row, the full amount at
// the horizon. Same weights as the per-layer model (fade 0.70, haze 0.60,
// cap 0.85), scaled by the slider.
static inline float mode7FogAmount(int fade, int haze, float slider)
{
    float t = (fade / 8.0f) * 0.70f + (haze / 8.0f) * 0.60f;
    t *= slider < 0.0f ? -slider : slider;
    return t > 0.85f ? 0.85f : t;
}

#endif
