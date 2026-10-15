#ifndef SCREEN_GRADE_H
#define SCREEN_GRADE_H

// Output colour grade (issue #77, "prettier colours"): saturation,
// contrast and brightness applied in the TexEnv stages of the final
// screen blit - the quad that already draws the game texture every
// frame - so it costs no extra pass. Pure parameter math here; the
// stage program lives in 3dsgpu.cpp (gpu3dsSetScreenGrade).
//
// Gauges -4..4 each (0 = untouched):
//   saturation s = 1 + sat/4   (0 = grey .. 2 = doubled)
//   contrast   g = 1 + con/8   (0.5 .. 1.5), around mid grey
//   brightness b = bri/16      (-0.25 .. +0.25), added after contrast
//
// Stage program (stage 0 = the blit's own replace of the texture):
//   1-2  contrast + brightness on RGB; luminance (76 R + 150 G + 29 B)
//        accumulates in alpha from the texture
//   3    RGB passthrough (written to the combiner buffer), alpha += B term
//   4    s <= 1: INTERPOLATE(rgb, luma, s); s > 1: max(rgb - luma, 0)
//   5    s <= 1: passthrough; s > 1: rgb + (s-1) x max(rgb - luma, 0)
//        (the buffer holds stage 3's rgb); alpha back to the blit's own

#define SCREEN_GRADE_MIN (-4)
#define SCREEN_GRADE_MAX   4

struct ScreenGrade
{
    bool  active;
    float sat, con, bri;      // s, g, b as above
    // stage constants, 0..255
    int   c1;                 // stage 1 RGB constant
    int   c2;                 // stage 2 RGB constant
    bool  c2x2;               // stage 2 scales x2 (contrast above 1)
    int   c4;                 // stage 4 constant (s <= 1: s; s > 1: unused)
    int   c5;                 // stage 5 constant (s > 1: s - 1; s <= 1: 0.5 + b for ADD_SIGNED)
};

// The game texture reaches the TexEnv with red and blue swapped relative
// to what the screen shows (measured on Azahar: a dark red texel's luma
// came out with the 29/76 weights exchanged), so the operand that reads
// "R" carries blue's weight and vice versa.
#define SCREEN_GRADE_LUMA_R_OP_WEIGHT  29
#define SCREEN_GRADE_LUMA_G_OP_WEIGHT 150
#define SCREEN_GRADE_LUMA_B_OP_WEIGHT  76

static inline int screen_grade_clamp(int v) { return v < SCREEN_GRADE_MIN ? SCREEN_GRADE_MIN : (v > SCREEN_GRADE_MAX ? SCREEN_GRADE_MAX : v); }
static inline int screen_grade_u8(float v) { int i = (int)(v * 255.0f + 0.5f); return i < 0 ? 0 : (i > 255 ? 255 : i); }

static inline ScreenGrade screen_grade_compute(int sat, int con, int bri)
{
    ScreenGrade g;
    sat = screen_grade_clamp(sat); con = screen_grade_clamp(con); bri = screen_grade_clamp(bri);
    g.active = sat != 0 || con != 0 || bri != 0;
    g.sat = 1.0f + (float)sat / 4.0f;
    g.con = 1.0f + (float)con / 8.0f;
    g.bri = (float)bri / 16.0f;
    // brightness: folded into the contrast constants when saturation is
    // above 1 (stage 5 is taken by the saturation), its own ADD_SIGNED in
    // stage 5 otherwise (the interpolation towards luma would otherwise
    // dilute it - the luma comes from the texture, before the contrast)
    const float bFold = g.sat > 1.0f ? g.bri : 0.0f;
    if (g.con <= 1.0f) {
        // x * g, then + (0.5 (1 - g) + b) via ADD_SIGNED (adds c - 0.5)
        g.c1 = screen_grade_u8(g.con);
        g.c2 = screen_grade_u8(0.5f + 0.5f * (1.0f - g.con) + bFold);
        g.c2x2 = false;
    } else {
        // (x + c1 - 0.5) * g  with  c1 - 0.5 = -0.5 (g - 1) / g + b / g, then x2 scale on g / 2
        g.c1 = screen_grade_u8(0.5f - 0.5f * (g.con - 1.0f) / g.con + bFold / g.con);
        g.c2 = screen_grade_u8(g.con * 0.5f);
        g.c2x2 = true;
    }
    g.c4 = g.sat <= 1.0f ? screen_grade_u8(g.sat) : 0;
    g.c5 = g.sat > 1.0f ? screen_grade_u8(g.sat - 1.0f) : screen_grade_u8(0.5f + g.bri);
    return g;
}

// reference of what the stages compute, for the tests (per channel, 0..1)
static inline float screen_grade_ref(const ScreenGrade &g, float r, float gr, float b, int channel)
{
    const float x = channel == 0 ? r : (channel == 1 ? gr : b);
    const float luma = (76.0f * r + 150.0f * gr + 29.0f * b) / 255.0f;
    float y = (x - 0.5f) * g.con + 0.5f + (g.sat > 1.0f ? g.bri : 0.0f);
    if (y < 0.0f) y = 0.0f;
    if (y > 1.0f) y = 1.0f;
    if (g.sat <= 1.0f) y = y * g.sat + luma * (1.0f - g.sat) + g.bri;
    else { float d = y - luma; if (d < 0.0f) d = 0.0f; y = y + (g.sat - 1.0f) * d; }
    return y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
}

#endif
