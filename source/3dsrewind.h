#ifndef _3DSREWIND_H
#define _3DSREWIND_H

// Rewind (issue #12): a ring of in-RAM savestates captured while playing,
// popped back while the Rewind hotkey is held.

// called once per emulated frame from emulatorLoop; rewindHeld is the
// live state of the Rewind (hold) hotkey, frameHadHeadroom tells whether
// the frame finished with enough vsync slack to hide a capture
void rewind3dsFrameTick(bool rewindHeld, bool frameHadHeadroom);

// drops all snapshots (call when a different ROM is loaded)
void rewind3dsReset();

#endif
