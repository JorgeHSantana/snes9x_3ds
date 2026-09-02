#ifndef _3DSLAYERUSE_H_
#define _3DSLAYERUSE_H_

#include <stdint.h>
#include <string.h>

// Which layer/priority rows actually drew something last frame - the 3D
// editor dims (or, opt-in, hides) the rows a paused screen does not use.
// Pure bookkeeping: the PPU walkers mark rows while a frame accumulates,
// the frame end latches the accumulator into the readable copy, so the
// editor always sees one complete frame, never a half-drawn one.
//
// Rows: layer 0..3 = BG1..BG4 with prio 0/1; layer 4 = sprites, prio 0..3.

#define LAYER_USE_LAYERS 5
#define LAYER_USE_PRIOS  4

struct LayerUse
{
    uint16_t acc[LAYER_USE_LAYERS][LAYER_USE_PRIOS];    // current frame
    uint16_t last[LAYER_USE_LAYERS][LAYER_USE_PRIOS];   // last latched frame
};

static inline void layerUseReset(LayerUse *u)
{
    memset(u, 0, sizeof(*u));
}

static inline void layerUseFrameStart(LayerUse *u)
{
    memset(u->acc, 0, sizeof(u->acc));
}

static inline void layerUseMark(LayerUse *u, int layer, int prio)
{
    if (layer < 0 || layer >= LAYER_USE_LAYERS || prio < 0 || prio >= LAYER_USE_PRIOS)
        return;
    if (u->acc[layer][prio] != 0xFFFF)
        u->acc[layer][prio]++;
}

static inline void layerUseFrameEnd(LayerUse *u)
{
    memcpy(u->last, u->acc, sizeof(u->last));
}

static inline bool layerUseUsed(const LayerUse *u, int layer, int prio)
{
    if (layer < 0 || layer >= LAYER_USE_LAYERS || prio < 0 || prio >= LAYER_USE_PRIOS)
        return false;
    return u->last[layer][prio] != 0;
}

// any priority of the layer drew
static inline bool layerUseLayerUsed(const LayerUse *u, int layer)
{
    for (int p = 0; p < LAYER_USE_PRIOS; p++)
        if (layerUseUsed(u, layer, p))
            return true;
    return false;
}

#endif
