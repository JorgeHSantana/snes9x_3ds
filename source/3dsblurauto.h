#ifndef _3DSBLURAUTO_H_
#define _3DSBLURAUTO_H_

// Blur Quality "Auto" (issue #71): Full while the frame budget holds,
// Light the moment the emulator skips a render frame, Full again only
// after a run of clean windows - and that run grows when Auto keeps
// flip-flopping (adaptive hysteresis, Jorge's ask). Pure logic: the
// frame loop feeds one bool per emulated frame, the renderer reads the
// verdict.
//
// A start is Light: the two eyes fuse one ghost each into the same smear
// Full's two ghosts give, so nothing is lost while the run of clean
// windows proves the game holds its rate - and an Old 3DS that never
// does never pays for Full (Jorge, Mario Kart). The first promotion is
// not a "return", so a skip right after it is not a relapse.
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
    int  warmup;       // frames still ignored after a (re)start: the first
                       // frames after a ROM or state load skip for reasons
                       // that are not load (caches filling, threads starting)
    bool light;        // current verdict
    bool promoted;     // Full reached at least once since the reset
};

#define BLUR_AUTO_WARMUP_FRAMES 60

static inline void blurAutoReset(BlurAutoState *s)
{
    s->frames = 0;
    s->skips = 0;
    s->clean = 0;
    s->required = BLUR_AUTO_CLEAN_BASE;
    s->fullWindows = BLUR_AUTO_STABLE_WINDOWS;   // a fresh start is not a relapse
    s->warmup = BLUR_AUTO_WARMUP_FRAMES;
    s->light = true;
    s->promoted = false;
}

// One emulated frame. Returns the verdict after this frame.
static inline bool blurAutoStep(BlurAutoState *s, bool skippedFrame)
{
    if (s == nullptr)
        return false;
    if (s->warmup > 0) {
        s->warmup--;
        skippedFrame = false;
    }

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
            // the first Full after a start is not a return: a skip soon
            // after it must not double the proof like a relapse would
            s->fullWindows = s->promoted ? 0 : BLUR_AUTO_STABLE_WINDOWS;
            s->promoted = true;
        }
    } else if (s->fullWindows < BLUR_AUTO_STABLE_WINDOWS) {
        s->fullWindows++;
    }
    s->frames = 0;
    s->skips = 0;
    return s->light;
}

#endif
