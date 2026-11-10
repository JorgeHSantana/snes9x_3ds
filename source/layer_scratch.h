#ifndef LAYER_SCRATCH_H
#define LAYER_SCRATCH_H

// Layer scratch image (perf-backlog B7; Jorge's GPU Cost Probe 2026-11-09:
// the PICA is vertex-bound, 0.38 us a vertex, and in 3D with the depth
// blur one 957-tile BG goes through the tile pipeline up to 12 times a
// frame - two targets, two eyes, the base pass and two ghosts).
//
// The layer's tiles are drawn ONCE a frame, unshifted, into a 512x256
// scratch image (one per priority plane: each plane has its own shift),
// and every pass then draws one textured quad per plane instead of the
// tiles. The scratch images live inside the Mode 7 texture, idle outside
// Mode 7: there is no VRAM left for a texture of their own.
//
// Pure logic, host-tested: what a layer's vertices must look like for the
// image to stand in for them, and whether the trade pays.

#define LAYER_SCRATCH_SLOTS       8     // 512x256 RGBA5551 images in the 1024x1024 Mode 7 texture
#define LAYER_SCRATCH_SLOT_BYTES  0x40000
#define LAYER_SCRATCH_IMG_W       512
#define LAYER_SCRATCH_IMG_H       256
#define LAYER_SCRATCH_SHIFT       128   // screen x 0 sits at image x 128: room for tiles hanging off both edges
#define LAYER_SCRATCH_MAX_PLANES  2     // a BG's two tile priorities

// measured on the console (docs/perf-backlog.md)
#define LAYER_SCRATCH_NS_VERTEX   380u
#define LAYER_SCRATCH_NS_PIXEL    9u

struct LayerScratchPlane {
    int plane;              // the depth plane in the vertex y (bits 8-12)
    int aa;                 // the alpha bits of the vertex y (bits 13-14), the same on every tile
    int x0, y0, x1, y1;     // bounding box of the plane's tiles, screen pixels (y = rows)
    int tiles;
    unsigned int tilePx;    // pixels the tiles cover (they do not overlap inside a BG)
};

struct LayerScratchScan {
    int nPlanes;
    LayerScratchPlane p[LAYER_SCRATCH_MAX_PLANES];   // ascending plane
};

// A tile vertex's y: 0aa0dddd yyyyyyyy (shader_tiles.v.pica)
static inline int layer_scratch_plane(int y) { return (y >> 8) & 0x1F; }
static inline int layer_scratch_aa(int y)    { return y & 0x6000; }

// Walks a layer's tile vertices (two a tile: top-left, bottom-right; xy
// points at the first vertex's x, y pair of 16-bit values, strideBytes
// apart). False when the image cannot stand in for them: a vertex the
// shader decodes differently (negative y, a tile crossing row 255, a
// degenerate tile), more than two planes, alpha bits that differ inside a
// plane (the quad carries one value), or a tile outside the image.
static inline bool layer_scratch_scan(const short *xy, int strideBytes, int count, LayerScratchScan *out)
{
    out->nPlanes = 0;
    if (count < 2 || (count & 1))
        return false;
    for (int i = 0; i + 1 < count; i += 2) {
        const short *a = (const short *)((const char *)xy + (long)i * strideBytes);
        const short *b = (const short *)((const char *)a + strideBytes);
        const int ax = a[0], ay = a[1], bx = b[0], by = b[1];
        if (ay < 0 || by < 0)
            return false;
        const int plane = layer_scratch_plane(ay), aa = layer_scratch_aa(ay);
        const int row0 = ay & 0xFF, h = by - ay, w = bx - ax;
        if (h <= 0 || w <= 0 || row0 + h > LAYER_SCRATCH_IMG_H - 1)
            return false;
        if (ax + LAYER_SCRATCH_SHIFT < 0 || bx + LAYER_SCRATCH_SHIFT > LAYER_SCRATCH_IMG_W)
            return false;
        int k = 0;
        while (k < out->nPlanes && out->p[k].plane != plane) k++;
        if (k == out->nPlanes) {
            if (out->nPlanes == LAYER_SCRATCH_MAX_PLANES)
                return false;
            // keep the planes ascending
            if (k == 1 && plane < out->p[0].plane) { out->p[1] = out->p[0]; k = 0; }
            out->nPlanes++;
            LayerScratchPlane *n = &out->p[k];
            n->plane = plane; n->aa = aa;
            n->x0 = ax; n->y0 = row0; n->x1 = bx; n->y1 = row0 + h;
            n->tiles = 0; n->tilePx = 0;
        }
        LayerScratchPlane *p = &out->p[k];
        if (p->aa != aa)
            return false;
        if (ax < p->x0) p->x0 = ax;
        if (bx > p->x1) p->x1 = bx;
        if (row0 < p->y0) p->y0 = row0;
        if (row0 + h > p->y1) p->y1 = row0 + h;
        p->tiles++;
        p->tilePx += (unsigned int)(w * h);
    }
    return true;
}

static inline unsigned int layer_scratch_bbox_px(const LayerScratchScan *s)
{
    unsigned int px = 0;
    for (int k = 0; k < s->nPlanes; k++)
        px += (unsigned int)((s->p[k].x1 - s->p[k].x0) * (s->p[k].y1 - s->p[k].y0));
    return px;
}

static inline unsigned int layer_scratch_tile_px(const LayerScratchScan *s)
{
    unsigned int px = 0;
    for (int k = 0; k < s->nPlanes; k++) px += s->p[k].tilePx;
    return px;
}

// Is the image cheaper than the tiles? verts: the layer's vertices;
// passes: how many times the frame draws them (targets x eyes x (base +
// ghosts)). Direct, every pass pays the vertices and the tiles' fill. With
// the image, the build pays the vertices once per plane (the other plane
// is shifted off the image but still runs the vertex shader), the box
// clear and the tiles' fill; every pass then pays two vertices a plane and
// the fill of the whole box - more than the tiles' where the layer is
// sparse. Taken only with a 20% margin: the constants are averages.
static inline bool layer_scratch_pays(unsigned int verts, unsigned int passes, unsigned int nPlanes,
    unsigned int bboxPx, unsigned int tilePx)
{
    if (passes < 2 || nPlanes == 0 || verts == 0)
        return false;
    const unsigned int direct = passes * (verts * LAYER_SCRATCH_NS_VERTEX + tilePx * LAYER_SCRATCH_NS_PIXEL);
    const unsigned int build = nPlanes * verts * LAYER_SCRATCH_NS_VERTEX + (bboxPx + tilePx) * LAYER_SCRATCH_NS_PIXEL;
    const unsigned int use = passes * (nPlanes * 2u * LAYER_SCRATCH_NS_VERTEX + bboxPx * LAYER_SCRATCH_NS_PIXEL);
    return (build + use) / 4u * 5u <= direct;
}

// The plane's tier in the shader's cascade: tier 1 from the layer's
// boundary up (plane / 32 >= boundary). The two planes of a layer must
// land on different tiers, or one build draw could not separate them.
static inline int layer_scratch_tier(int plane, float boundary)
{
    return (float)plane * (1.0f / 32.0f) >= boundary ? 1 : 0;
}

#endif
