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
// a Mode 7 plane drew in the last rendered frame (editor: the Mode 7 gauge dims otherwise)
bool S9xMode7DrawnLastFrame();
// sprites follow the Mode 7 ground (issue #76): the last drawn frame's
// plane rows and sprite signature table (3dsgroundsprites.h)
bool     S9xGroundRowsLastFrame();
int      S9xGroundSigCount();
uint32_t S9xGroundSig(int slot);
void     S9xGroundSigPos(int slot, int *x, int *y);

#endif