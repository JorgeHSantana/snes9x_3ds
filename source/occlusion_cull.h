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

// Can section C (a candidate coverer) hide tiles of section V? Both on
// the same target and the same vertical segment; C full-screen opaque, no
// window; C's lowest priority plane above at least V's lowest (a V tile
// is then hidden per tile, by its own plane: in Mode 1 with the BG3
// priority bit a BG3 priority-1 tile sits above everything and stays).
static inline bool occl_section_covers(bool cFullOpaque, bool cStencilDisabled,
    int cPlaneMin, int vPlaneMin, int cSegY0, int cSegY1, int vSegY0, int vSegY1)
{
    return cFullOpaque && cStencilDisabled && cPlaneMin > vPlaneMin &&
           cSegY0 == vSegY0 && cSegY1 == vSegY1;
}

// A tile vertex's y carries its depth plane in bits 8-11 (gfxhw: y =
// screenY + plane * 256 + the alpha bits above).
static inline int occl_vertex_plane(int y) { return (y >> 8) & 0xF; }

// The x span [L, R] the coverer is guaranteed to fill this eye, in vertex
// units: its tiles shift by sc0 (priority 0) or sc1 (priority 1), so the
// screen [0, width] is covered where both shifted copies would be.
static inline void occl_cover_span(float sc0, float sc1, int width, float *L, float *R)
{
    *L = sc0 > sc1 ? sc0 : sc1;
    *R = (float)width + (sc0 < sc1 ? sc0 : sc1);
}

// A covered tile [x0, x1] on plane `plane`, shifted by sv0 or sv1 (both
// are tried), is hidden when its plane is below the coverer's lowest and
// it lies strictly inside [L, R] with a 1 px margin for fractional
// (Continuous) shifts.
static inline bool occl_tile_hidden(int x0, int x1, int plane, int cPlaneMin, float sv0, float sv1, float L, float R)
{
    if (plane >= cPlaneMin) return false;
    const float svMin = sv0 < sv1 ? sv0 : sv1, svMax = sv0 > sv1 ? sv0 : sv1;
    return (float)x0 + svMin >= L + 1.0f && (float)x1 + svMax <= R - 1.0f;
}


// ---- window cull -------------------------------------------------------
// The SNES windows reach the GPU as a stencil (gfxhw S9xCommitWindowLRSection
// draws, per vertical section of constant window edges, rects whose stencil
// byte is (in1 | in2 << 1 | (in1 ^ in2) << 2) << 5, in1/in2 = inside window
// 1/2, left..right inclusive - cliphw ComputeClipWindowsForStenciling), and
// a windowed BG section tests (stencil & inputMask) func ref. A tile whose
// every pixel fails that test draws nothing: skip it. MMX3 ZP's dam keeps a
// full-screen BG3 on the high priority, windowed down to the water: half its
// 928 tiles are stenciled away in every pass of both eyes.

typedef struct {
    int startY, endY;           // rows, inclusive
    int w1L, w1R, w2L, w2R;     // window edges, inclusive (left > right = empty)
} OcclWindow;

static inline int occl_window_mask(int x, int w1L, int w1R, int w2L, int w2R)
{
    const int in1 = (x >= w1L && x <= w1R) ? 1 : 0;
    const int in2 = (x >= w2L && x <= w2R) ? 1 : 0;
    return (in1 | (in2 << 1) | ((in1 ^ in2) << 2)) << 5;
}

// the packed stencil state (gfxhw S9xComputeAndEnableStencilFunction):
// bit 0 enabled, bits 4-6 func (0 NEVER, 1 ALWAYS, 2 EQUAL, 3 NOTEQUAL),
// bits 16-23 ref, bits 24-31 input mask. Only EQUAL / NOTEQUAL are culled.
static inline bool occl_stencil_cullable(unsigned st)
{
    const unsigned func = (st >> 4) & 7u;
    return (st & 1u) && (func == 2u || func == 3u);
}

static inline bool occl_stencil_pass(unsigned st, int stencilByte)
{
    const unsigned func = (st >> 4) & 7u, ref = (st >> 16) & 0xFFu, mask = (st >> 24) & 0xFFu;
    const unsigned v = (unsigned)stencilByte & mask;
    if (func == 2u) return v == ref;
    if (func == 3u) return v != ref;
    return func != 0u;
}

// does any pixel of x in [xa, xb) pass the test under window section w?
// The mask is piecewise constant between the window edges, so only xa and
// the edges inside the span are tried. x outside 0..255 is off-screen.
static inline bool occl_window_span_passes(unsigned st, const OcclWindow *w, int xa, int xb)
{
    if (xa < 0) xa = 0;
    if (xb > 256) xb = 256;
    if (xb <= xa) return false;
    const int edges[5] = { xa, w->w1L, w->w1R + 1, w->w2L, w->w2R + 1 };
    for (int i = 0; i < 5; i++) {
        const int x = edges[i];
        if (x < xa || x >= xb) continue;
        if (occl_stencil_pass(st, occl_window_mask(x, w->w1L, w->w1R, w->w2L, w->w2R))) return true;
    }
    return false;
}

// a tile covering rows [row0, row1) and x [xa, xb) is windowed out when
// every row lies in a drawn window section (ascending, contiguous as
// committed) and no pixel passes; a row no section covers holds whatever
// the stencil held before -> not culled
static inline bool occl_tile_windowed_out(unsigned st, int row0, int row1, int xa, int xb, const OcclWindow *w, int n)
{
    if (row1 <= row0) return false;
    int next = row0;
    for (int i = 0; i < n; i++) {
        if (w[i].endY < next) continue;
        if (w[i].startY > next) return false;
        if (occl_window_span_passes(st, &w[i], xa, xb)) return false;
        next = w[i].endY + 1;
        if (next >= row1) return true;
    }
    return false;
}

#endif
