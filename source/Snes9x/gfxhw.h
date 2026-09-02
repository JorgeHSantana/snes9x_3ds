#include "copyright.h"


#ifndef _GFXHW_H_
#define _GFXHW_H_

extern "C" void S9xRenderScreenHardware (bool8 sub, bool8 force_no_add, uint8 D);

// per-frame layer/priority usage for the 3D editor (3dslayeruse.h):
// frame start/end are called by gfx.cpp's screen refresh, the query by
// the menu while paused. layer 0..3 = BG1..BG4 (prio 0/1), 4 = sprites
// (prio 0..3).
void S9xLayerUseFrameStart();
void S9xLayerUseFrameEnd();
bool S9xLayerUsedLastFrame(int layer, int prio);
bool S9xLayerUsedLastFrameAny(int layer);

#endif