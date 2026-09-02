#ifndef _3DSBLURAUTO_H_
#define _3DSBLURAUTO_H_

// Blur Quality "Auto" (issue #71): Full while the frame budget holds,
// Light the moment the emulator skips a render frame, Full again only
// after a run of clean windows - and that run grows when Auto keeps
// flip-flopping (adaptive hysteresis, Jorge's ask). Pure logic: the
// frame loop feeds one bool per emulated frame, the renderer reads the
// verdict.
//
// Entering Light is immediate: one skipped frame flips it. Returning to
// Full needs `required` consecutive clean windows of
// BLUR_AUTO_WINDOW_FRAMES frames; any skip while Light restarts the run.
// `required` starts at BLUR_AUTO_CLEAN_BASE, doubles (up to
// BLUR_AUTO_CLEAN_MAX) whenever Light comes back less than
// BLUR_AUTO_RELAPSE_WINDOWS after a return to Full, and resets to base
// once Full has held for BLUR_AUTO_STABLE_WINDOWS.

#define BLUR_AUTO_WINDOW_FRAMES    60
#define BLUR_AUTO_CLEAN_BASE        3
#define BLUR_AUTO_CLEAN_MAX        60
#define BLUR_AUTO_RELAPSE_WINDOWS  10
#define BLUR_AUTO_STABLE_WINDOWS   30

struct BlurAutoState
{
    int  frames;       // frames seen in the current window
    int  skips;        // skipped frames in the current window
    int  clean;        // consecutive clean windows while Light
    int  required;     // clean windows Full needs right now (adapts)
    int  fullWindows;  // windows since the last return to Full (saturates)
    bool light;        // current verdict
};

static inline void blurAutoReset(BlurAutoState *s)
{
    s->frames = 0;
    s->skips = 0;
    s->clean = 0;
    s->required = BLUR_AUTO_CLEAN_BASE;
    s->fullWindows = BLUR_AUTO_STABLE_WINDOWS;   // a fresh start is not a relapse
    s->light = false;
}

// One emulated frame. Returns the verdict after this frame.
static inline bool blurAutoStep(BlurAutoState *s, bool skippedFrame)
{
    if (s == nullptr)
        return false;

    if (!s->light && skippedFrame) {
        // Full just missed a frame: Light now. How soon after the last
        // return to Full decides how much proof the next return needs.
        if (s->fullWindows < BLUR_AUTO_RELAPSE_WINDOWS) {
            s->required *= 2;
            if (s->required > BLUR_AUTO_CLEAN_MAX) s->required = BLUR_AUTO_CLEAN_MAX;
        } else if (s->fullWindows >= BLUR_AUTO_STABLE_WINDOWS) {
            s->required = BLUR_AUTO_CLEAN_BASE;
        }
        s->light = true;
        s->clean = 0;
        s->frames = 0;
        s->skips = 0;
        return true;
    }

    if (skippedFrame)
        s->skips++;
    if (++s->frames < BLUR_AUTO_WINDOW_FRAMES)
        return s->light;

    // window closed
    if (s->light) {
        if (s->skips > 0)
            s->clean = 0;                         // not clean: the run restarts
        else if (++s->clean >= s->required) {
            s->light = false;
            s->fullWindows = 0;
        }
    } else if (s->fullWindows < BLUR_AUTO_STABLE_WINDOWS) {
        s->fullWindows++;
    }
    s->frames = 0;
    s->skips = 0;
    return s->light;
}

#endif
