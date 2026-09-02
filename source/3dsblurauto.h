#ifndef _3DSBLURAUTO_H_
#define _3DSBLURAUTO_H_

// Blur Quality "Auto" (issue #71): Full while the frame budget holds,
// Light once the emulator starts skipping render frames, back to Full
// after a run of clean windows. Pure logic, no platform state - the
// frame loop feeds one bool per emulated frame and the renderer reads
// the verdict.
//
// A window is BLUR_AUTO_WINDOW_FRAMES emulated frames. A window with at
// least BLUR_AUTO_SKIP_TRIGGER skipped frames switches to Light and arms
// BLUR_AUTO_CLEAN_WINDOWS of cooldown; each fully clean window burns one,
// any skipped frame re-arms it. Full returns when the cooldown reaches 0.

#define BLUR_AUTO_WINDOW_FRAMES 60
#define BLUR_AUTO_SKIP_TRIGGER  2
#define BLUR_AUTO_CLEAN_WINDOWS 3

struct BlurAutoState
{
    int  frames;     // frames seen in the current window
    int  skips;      // skipped frames in the current window
    int  cooldown;   // clean windows still needed before Full returns
    bool light;      // current verdict
};

static inline void blurAutoReset(BlurAutoState *s)
{
    s->frames = 0;
    s->skips = 0;
    s->cooldown = 0;
    s->light = false;
}

// One emulated frame. Returns the verdict after this frame.
static inline bool blurAutoStep(BlurAutoState *s, bool skippedFrame)
{
    if (s == nullptr)
        return false;
    if (skippedFrame)
        s->skips++;
    if (++s->frames < BLUR_AUTO_WINDOW_FRAMES)
        return s->light;

    // window closed: judge it
    if (s->skips >= BLUR_AUTO_SKIP_TRIGGER) {
        s->light = true;
        s->cooldown = BLUR_AUTO_CLEAN_WINDOWS;
    } else if (s->light) {
        if (s->skips > 0)
            s->cooldown = BLUR_AUTO_CLEAN_WINDOWS;   // not clean: re-arm
        else if (--s->cooldown <= 0)
            s->light = false;
    }
    s->frames = 0;
    s->skips = 0;
    return s->light;
}

#endif
