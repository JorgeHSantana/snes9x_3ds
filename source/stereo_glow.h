#ifndef STEREO_GLOW_H
#define STEREO_GLOW_H

#include <stdint.h>

// Glow per depth row (issue #77, the bloom-like half): the marked rows'
// bright texels are drawn again, additively, in their own colours, as a
// small halo. Pure helpers; the GPU side lives in 3dsimpl_gpu.cpp.
//
// Selection by brightness: TexEnv computes the texel's luminance
// (76 R + 150 G + 29 B, /255) into alpha and the alpha test keeps
// texels at or above the threshold; the blend then adds
// colour x tint x strength x luminance - brighter texels add more.

#define STEREO_GLOW_THRESH_MAX   15   // threshold gauge: ref = thresh x 16 (0 = everything but transparent)
#define STEREO_GLOW_STRENGTH_MAX  8   // strength gauge in quarters: 1 = x0.25 .. 8 = x2
#define STEREO_GLOW_SOFT_MAX      2   // 0 = in place, 1 = plus the 4 neighbours, 2 = 3x3 box
#define STEREO_GLOW_COLORS        5

static inline int stereo_glow_clamp_thresh(int v) { return v < 0 ? 0 : (v > STEREO_GLOW_THRESH_MAX ? STEREO_GLOW_THRESH_MAX : v); }
static inline int stereo_glow_clamp_strength(int v) { return v < 1 ? 1 : (v > STEREO_GLOW_STRENGTH_MAX ? STEREO_GLOW_STRENGTH_MAX : v); }
static inline int stereo_glow_clamp_soft(int v) { return v < 0 ? 0 : (v > STEREO_GLOW_SOFT_MAX ? STEREO_GLOW_SOFT_MAX : v); }

// alpha test reference (0..255); 0 would pass transparent texels, so at least 1
static inline int stereo_glow_alpha_ref(int thresh)
{
    const int r = stereo_glow_clamp_thresh(thresh) * 16;
    return r < 1 ? 1 : (r > 255 ? 255 : r);
}

static inline int stereo_glow_copies(int soft)
{
    soft = stereo_glow_clamp_soft(soft);
    return soft == 0 ? 1 : (soft == 1 ? 5 : 9);
}
// copy 0 is the centre; soft 1 adds (+-1,0),(0,+-1); soft 2 the 3x3 box
static inline int stereo_glow_dx(int soft, int copy)
{
    if (copy == 0) return 0;
    if (stereo_glow_clamp_soft(soft) == 1) { static const int d[5] = { 0, 1, -1, 0, 0 }; return d[copy < 5 ? copy : 0]; }
    static const int d[9] = { 0, 1, -1, 0, 0, 1, -1, 1, -1 }; return d[copy < 9 ? copy : 0];
}
static inline int stereo_glow_dy(int soft, int copy)
{
    if (copy == 0) return 0;
    if (stereo_glow_clamp_soft(soft) == 1) { static const int d[5] = { 0, 0, 0, 1, -1 }; return d[copy < 5 ? copy : 0]; }
    static const int d[9] = { 0, 0, 0, 1, -1, 1, 1, -1, -1 }; return d[copy < 9 ? copy : 0];
}

// Per-copy gain so the centre (covered by every copy) adds strength x
// colour and the halo's edge a fraction: gain = strength / copies.
// The TexEnv constant holds min(gain, 1) and the stage scales x2 when
// the gain is above 1 (gain is at most 2).
static inline float stereo_glow_copy_gain(int strength, int soft)
{
    return (float)stereo_glow_clamp_strength(strength) * 0.25f / (float)stereo_glow_copies(soft);
}
static inline int stereo_glow_env_const(float gain) { float g = gain > 1.0f ? gain * 0.5f : gain; int v = (int)(g * 255.0f + 0.5f); return v > 255 ? 255 : v; }
static inline bool stereo_glow_env_x2(float gain) { return gain > 1.0f; }

static inline const char* stereo_glow_color_name(int idx)
{
    static const char* names[STEREO_GLOW_COLORS] = { "Own colours", "Warm", "Cool", "Gold", "White" };
    return names[idx >= 0 && idx < STEREO_GLOW_COLORS ? idx : 0];
}
// the tint the texel colour is multiplied by (RGB 0..255)
static inline uint32_t stereo_glow_color_rgb(int idx)
{
    static const uint32_t rgb[STEREO_GLOW_COLORS] = { 0xFFFFFF, 0xFFC080, 0x80C0FF, 0xFFE040, 0xFFFFFF };
    return rgb[idx >= 0 && idx < STEREO_GLOW_COLORS ? idx : 0];
}

#endif
