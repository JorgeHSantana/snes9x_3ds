#ifndef _3DSREWIND_H
#define _3DSREWIND_H

#include <stdint.h>

// Rewind (issue #12): a ring of in-RAM savestates captured while playing,
// popped back while the Rewind hotkey is held.

// called once per emulated frame from emulatorLoop; rewindHeld is the
// live state of the Rewind (hold) hotkey, frameHadHeadroom tells whether
// the frame finished with enough vsync slack to hide a capture
void rewind3dsFrameTick(bool rewindHeld, bool frameHadHeadroom);

// drops all snapshots (call when a different ROM is loaded)
void rewind3dsReset();

// --- timeline (3dsrewindui.cpp) --------------------------------------------

#define REWIND_THUMB_W      100
#define REWIND_THUMB_H      60
#define REWIND_THUMB_BYTES  (REWIND_THUMB_W * REWIND_THUMB_H * 2)   // RGB565

bool rewind3dsTakeTimelineRequest();      // hotkey press or menu action
void rewind3dsRequestTimelineFromMenu();  // menu entry: B returns to the menu
bool rewind3dsTimelineFromMenu();
bool rewind3dsTimelineActive();           // joypad reads freeze while true
void rewind3dsSetTimelineActive(bool active);

int      rewind3dsCount();                // snapshots stored (0 = newest first)
uint32_t rewind3dsNowFrame();
bool     rewind3dsPeekInfo(int back, uint32_t *frameTag);
const uint8_t *rewind3dsThumb(int back);  // 100x60 RGB565 row-major, or null

bool rewind3dsCapturePresent();           // snapshot "now" on timeline entry
bool rewind3dsRestorePresent();
bool rewind3dsRestoreAt(int back);        // materialize without consuming
void rewind3dsRollbackTo(int back);       // commit: drop the future branch

void rewind3dsMsuDeferBegin();            // silence + latch MSU while browsing
void rewind3dsMsuDeferEnd();              // apply latched MSU state in one go

// the modal timeline screen itself (blocks until closed)
void rewind3dsTimelineShow();

// the menu's real Yes/No dialog, runnable from the game context
// (implemented in 3dsmain.cpp - it owns the menu tab globals)
bool rewind3dsConfirmResume();

// --- input tape (rewind v2 degrau 2, docs/rewind-v2-spec.md A.1) -----------

// called by S9xReadJoypad with the effective (post-turbo) pad it returns;
// the value is committed to the tape by the frame's rewind3dsFrameTick
void rewind3dsNotePad(uint32_t pad);

// MSU-1 status-port read hook (registered with msu1_set_status_read_hook):
// records the SD-timing-dependent values a replay must feed back
void rewind3dsNoteMsuStatus(uint8_t value);

// --- v2 window engine (degrau 3, New 3DS) -----------------------------------

// while a replay re-executes tape frames, the joypad and MSU status port
// are fed from the recording instead of live input/state
bool     rewind3dsReplayActive();
uint32_t rewind3dsReplayPad();
bool     rewind3dsMsuStatusOverride(uint8_t *value);   // msu1_set_status_override

// frames of replay a jump to 'back' would need right now (0 = resident,
// -1 = unreachable); the timeline shows "Loading..." past a threshold
int  rewind3dsEstimateRestoreFrames(int back);

// bounded background replay slice toward the missing tick nearest to the
// cursor; call once per timeline UI frame while browsing
void rewind3dsPrefetchStep(int cursorBack);

#endif
