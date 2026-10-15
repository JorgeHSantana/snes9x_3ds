#ifndef STEREO_SHADOW_H
#define STEREO_SHADOW_H

#include <stdint.h>

// Drop shadows per depth row (issue #77): the 12 rows of the 3D tab's
// Depth list, as a bit mask, and the depth the shadow of a row takes -
// the nearest row behind it in the profile's gauges, so the silhouette
// sits on that surface.
//
// Row order (bit index): BG1 P0, BG1 P1, BG2 P0, BG2 P1, BG3 P0, BG3 P1,
// BG4 P0, BG4 P1, OBJ P0, OBJ P1, OBJ P2, OBJ P3.

#define STEREO_SHADOW_ROWS 12
#define STEREO_SHADOW_OFFSET_MAX 4
#define STEREO_SHADOW_STRETCH_MAX 8

static inline int stereo_shadow_row(int layer, int prio)
{
    if (layer < 0 || layer > 4 || prio < 0) return -1;
    if (layer < 4) return prio > 1 ? -1 : layer * 2 + prio;
    return prio > 3 ? -1 : 8 + prio;
}

static inline bool stereo_shadow_marked(unsigned int mask, int layer, int prio)
{
    const int r = stereo_shadow_row(layer, prio);
    return r >= 0 && ((mask >> r) & 1u) != 0;
}

// depth[12] in row order, usedMask = rows the scene draws (0 = consider
// all). Returns the depth the row's shadow sits at: the largest depth
// strictly smaller than the row's own among the other used rows, or the
// row's own depth when nothing is behind it.
static inline int stereo_shadow_behind_depth(const int depth[STEREO_SHADOW_ROWS], unsigned int usedMask, int row)
{
    if (row < 0 || row >= STEREO_SHADOW_ROWS) return 0;
    const int own = depth[row];
    int best = own; bool found = false;
    for (int r = 0; r < STEREO_SHADOW_ROWS; r++) {
        if (r == row) continue;
        if (usedMask != 0 && ((usedMask >> r) & 1u) == 0) continue;
        if (depth[r] < own && (!found || depth[r] > best)) { best = depth[r]; found = true; }
    }
    return best;
}

// Shadow colours the editor offers (index into this table, saved as
// SHADOWCOLOR). RGB, alpha comes from the pass.
#define STEREO_SHADOW_COLORS 6
static inline const char* stereo_shadow_color_name(int idx)
{
    static const char* names[STEREO_SHADOW_COLORS] = { "Black", "Dark gray", "Navy", "Brown", "Purple", "White" };
    return names[idx >= 0 && idx < STEREO_SHADOW_COLORS ? idx : 0];
}
static inline uint32_t stereo_shadow_color_rgb(int idx)
{
    static const uint32_t rgb[STEREO_SHADOW_COLORS] = { 0x000000, 0x303030, 0x101840, 0x301808, 0x281040, 0xFFFFFF };
    return rgb[idx >= 0 && idx < STEREO_SHADOW_COLORS ? idx : 0];
}

// The row the shadow lands on: the used row with the largest depth
// strictly smaller than the caster's, -1 when nothing is behind it.
static inline int stereo_shadow_behind_row(const int depth[STEREO_SHADOW_ROWS], unsigned int usedMask, int row)
{
    if (row < 0 || row >= STEREO_SHADOW_ROWS) return -1;
    const int own = depth[row];
    int best = -1;
    for (int r = 0; r < STEREO_SHADOW_ROWS; r++) {
        if (r == row) continue;
        if (usedMask != 0 && ((usedMask >> r) & 1u) == 0) continue;
        if (depth[r] < own && (best < 0 || depth[r] > depth[best])) best = r;
    }
    return best;
}

// The renderer's depth plane of a row (the z every pixel of that row
// writes, d in the shader's d/32 space): BG rows read the per-mode table
// gfxhw fills next to the priority boundary (row order, bg*2+prio), OBJ
// rows are fixed at (priority+1)*3 like S9xDrawOBJSHardware. -1 = no row.
static inline int stereo_shadow_row_plane(const int bgPlane[8], int row)
{
    if (row < 0 || row >= STEREO_SHADOW_ROWS) return -1;
    return row < 8 ? bgPlane[row] : (row - 8 + 1) * 3;
}

// The planes a row's shadow is tested against: every used row at the
// depth behind the caster - one surface in 3D; the gauges, not the SNES
// order, say what is "behind" (Jorge) - except a row sharing the caster's
// own plane, which the depth buffer cannot tell apart from its body.
// Returns the count written to out[] (distinct, at most 8).
static inline int stereo_shadow_receiver_planes(const int depth[STEREO_SHADOW_ROWS], unsigned int usedMask,
                                                const int bgPlane[8], int row, int out[8])
{
    const int br = stereo_shadow_behind_row(depth, usedMask, row);
    if (br < 0) return 0;
    const int casterPlane = stereo_shadow_row_plane(bgPlane, row);
    int n = 0;
    for (int r = 0; r < STEREO_SHADOW_ROWS; r++) {
        if (r == row || depth[r] != depth[br]) continue;
        if (usedMask != 0 && ((usedMask >> r) & 1u) == 0) continue;
        const int pl = stereo_shadow_row_plane(bgPlane, r);
        if (pl < 0 || pl == casterPlane) continue;
        bool dup = false;
        for (int k = 0; k < n; k++) if (out[k] == pl) dup = true;
        if (!dup && n < 8) out[n++] = pl;
    }
    return n;
}

// Stretch X / Y: the silhouette scaled about each sprite's feet (bottom
// centre of its OAM rectangle), gauge -3..8 = x0.25 .. x3 in quarters.
#define STEREO_SHADOW_STRETCH_MIN (-3)
static inline int stereo_shadow_clamp_stretch(int v)
{
    return v < STEREO_SHADOW_STRETCH_MIN ? STEREO_SHADOW_STRETCH_MIN : (v > STEREO_SHADOW_STRETCH_MAX ? STEREO_SHADOW_STRETCH_MAX : v);
}
static inline float stereo_shadow_stretch_factor(int v)
{
    return 1.0f + 0.25f * (float)stereo_shadow_clamp_stretch(v);
}

static inline int stereo_shadow_clamp_offset(int v)
{
    return v < -STEREO_SHADOW_OFFSET_MAX ? -STEREO_SHADOW_OFFSET_MAX : (v > STEREO_SHADOW_OFFSET_MAX ? STEREO_SHADOW_OFFSET_MAX : v);
}

#endif
