#ifndef _3DSREWIND_H
#define _3DSREWIND_H

// Rewind (issue #12): a ring of in-RAM savestates captured while playing,
// popped back while the Rewind hotkey is held.

// called once per emulated frame from emulatorLoop; rewindHeld is the
// live state of the Rewind hotkey, frameLoadPercent is how much of the
// frame budget this frame used (0-100+; low = idle, capture-friendly)
void rewind3dsFrameTick(bool rewindHeld, int frameLoadPercent);

// drops all snapshots (call when a different ROM is loaded)
void rewind3dsReset();

// --- timeline (3dsrewindui.cpp) --------------------------------------------

#define REWIND_THUMB_W      150
#define REWIND_THUMB_H      90
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

#endif
