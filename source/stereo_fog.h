#ifndef STEREO_FOG_H
#define STEREO_FOG_H

// Fade / haze per priority tier in ONE pass (Jorge's GPU Cost Probe,
// 2026-11-06: in MMX3's heavy phase the probe's "no fade/haze/blur" step
// took the GPU wait from 12.6 ms to zero - every layer was redrawn once
// per depth group so each group could carry its own constant tint, and
// the alpha-hidden groups still cost their fill). The tile vertex shader
// now picks the tier's fog amount from a 4-float uniform, the same way it
// picks the parallax, and writes it into the vertex colour's blue; the
// TexEnv interpolates towards the fog colour by that blue. One base pass
// per layer whatever the depths; only the blur ghosts still multiply.
// Pure math, host-tested.

// the tiles geometry shader scales the vertex colour by 1/248 on its way
// to the fragment: the uniform carries the amount pre-multiplied
#define STEREO_FOG_VERTEX_SCALE 248.0f
#define STEREO_FOG_MAX 0.85f
#define STEREO_FOG_MIN 0.01f   // below this nothing tints: the stage stays passthrough

// the tint amount for one depth: gauge strength scaled by how far behind
// the focus zone it sits (0..1) and by the slider
static inline float stereo_fog_amount(float fade, float haze, float depthIn, float slider)
{
    if (slider < 0.0f) slider = -slider;
    const float f = (fade / 8.0f) * depthIn * slider * 0.70f;
    const float hz = (haze / 8.0f) * depthIn * slider * 0.60f;
    const float t = f + hz;
    return t > STEREO_FOG_MAX ? STEREO_FOG_MAX : t;
}

// the haze share of the tint picks the target colour; the same for every
// depth, since fade and haze scale with the depth together
static inline float stereo_fog_share(float fade, float haze)
{
    const float f = fade * 0.70f, hz = haze * 0.60f;
    return (f + hz) > 0.0f ? hz / (f + hz) : 0.0f;
}

// the TexEnv constant (ABGR, alpha opaque): black for pure fade, the
// pale blue-grey haze for pure haze
static inline unsigned stereo_fog_color(float share)
{
    const unsigned r = (unsigned)(200.0f * share), g = (unsigned)(205.0f * share), b = (unsigned)(215.0f * share);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

#endif
