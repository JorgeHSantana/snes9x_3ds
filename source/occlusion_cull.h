#ifndef OCCLUSION_CULL_H
#define OCCLUSION_CULL_H

// BG occlusion cull (Jorge's GPU Cost Probe, 2026-11-07): the PICA spends
// a flat 0.8 us per vertex on this pipeline, and MMX3 ZP's heavy phase
// hands it two full 957-quad BGs of which the back one is entirely
// hidden behind the front one - every tile of the front BG is opaque
// (no transparent texel), it has no window, and both its priorities sit
// above both of the back BG's. Those hidden tiles are skipped at draw
// time. In 3D the two layers shift by different amounts, so a strip up to
// the shift difference wide at one screen edge stays uncovered: tiles
// touching it are kept. Pure logic, host-tested; the render reads the
// vertex x of each tile and the layers' shifts.

// Does section C (a candidate coverer) hide what section V draws?
// Both on the same target and the same vertical segment; C's lowest
// priority plane above V's highest; C full-screen opaque, no window.
static inline bool occl_section_covers(bool cFullOpaque, bool cStencilDisabled,
    int cPlaneMin, int vPlaneMax, int cSegY0, int cSegY1, int vSegY0, int vSegY1)
{
    return cFullOpaque && cStencilDisabled && cPlaneMin > vPlaneMax &&
           cSegY0 == vSegY0 && cSegY1 == vSegY1;
}

// The x span [L, R] the coverer is guaranteed to fill this eye, in vertex
// units: its tiles shift by sc0 (priority 0) or sc1 (priority 1), so the
// screen [0, width] is covered where both shifted copies would be.
static inline void occl_cover_span(float sc0, float sc1, int width, float *L, float *R)
{
    *L = sc0 > sc1 ? sc0 : sc1;
    *R = (float)width + (sc0 < sc1 ? sc0 : sc1);
}

// A covered tile [x0, x1] shifted by sv0 or sv1 (its priority is not
// decoded: both are tried) lies strictly inside [L, R] with a 1 px
// margin for fractional (Continuous) shifts -> hidden, skip it.
static inline bool occl_tile_hidden(int x0, int x1, float sv0, float sv1, float L, float R)
{
    const float svMin = sv0 < sv1 ? sv0 : sv1, svMax = sv0 > sv1 ? sv0 : sv1;
    return (float)x0 + svMin >= L + 1.0f && (float)x1 + svMax <= R - 1.0f;
}

#endif
