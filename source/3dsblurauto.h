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
// Off tier (Jorge, MMX3's water stage on an Old 3DS): Light still
// dropping frames for this many consecutive windows turns the blur off;
// BLUR_AUTO_OFF_CLEAN clean windows bring Light back.
#define BLUR_AUTO_OFF_AFTER         2
#define BLUR_AUTO_OFF_CLEAN         3

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
    bool off;          // Off tier engaged (only reported when the caller allows it)
    int  lightDirty;   // consecutive Light windows with skips (Off trigger)
    int  offClean;     // consecutive clean windows while Off
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
    s->off = false;
    s->lightDirty = 0;
    s->offClean = 0;
}

// the verdict as a tier: 0 Full, 1 Light, 2 Off
static inline int blurAutoTier(const BlurAutoState *s, bool allowOff)
{
    if (s == nullptr) return 0;
    if (allowOff && s->off) return 2;
    return s->light ? 1 : 0;
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
        // Off tier bookkeeping: Light that keeps dropping frames escalates,
        // Off that runs clean de-escalates (back to Light, then the Full
        // rule below applies as usual)
        bool leftOff = false;
        if (s->off) {
            if (s->skips > 0) s->offClean = 0;
            else if (++s->offClean >= BLUR_AUTO_OFF_CLEAN) { s->off = false; s->offClean = 0; s->lightDirty = 0; leftOff = true; }
        } else if (s->skips > 0) {
            if (++s->lightDirty >= BLUR_AUTO_OFF_AFTER) { s->off = true; s->offClean = 0; }
        } else {
            s->lightDirty = 0;
        }
        if (s->skips > 0)
            s->clean = 0;                         // not clean: the run restarts
        else if (s->off || leftOff)
            s->clean = 0;                         // Off first goes back to Light; Full's run starts after
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
