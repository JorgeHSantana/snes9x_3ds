#ifndef SIG_THROTTLE_H
#define SIG_THROTTLE_H

#include <stdint.h>

// Throttle for the scene-signature log line ("[sig] ..."). A register
// that flips every frame (Zelda ALTTP's 210B alternates 00/04 through
// the whole intro) made the line fire 30 times a second for minutes -
// megabytes of log and, on an Old 3DS, I/O in the middle of a heavy
// scene. Two rules: a signature seen among the last HISTORY logged ones
// is not logged again (absorbs A/B/A/B toggles), and at most LINES_PER_WINDOW
// lines go out per window; what was dropped is summarised once per
// SUMMARY_COOLDOWN windows. Pure, tested on the host.
struct SigThrottle
{
    static constexpr int HISTORY = 4;
    static constexpr int WINDOW_FRAMES = 60;
    static constexpr int LINES_PER_WINDOW = 8;
    static constexpr int SUMMARY_COOLDOWN = 10;   // windows

    uint64_t histA[HISTORY], histB[HISTORY];
    int      histPos;
    int      winFrames, winLines;
    uint32_t dropped;         // since the last summary
    int      cooldown;        // windows until a summary may be written

    void reset()
    {
        for (int i = 0; i < HISTORY; i++) { histA[i] = ~0ULL; histB[i] = ~0ULL; }
        histPos = 0; winFrames = 0; winLines = 0; dropped = 0; cooldown = 0;
    }

    // a signature change happened this frame: log it?
    bool admit(uint64_t a, uint64_t b)
    {
        bool seen = false;
        for (int i = 0; i < HISTORY; i++)
            if (histA[i] == a && histB[i] == b) { seen = true; break; }
        if (!seen) {
            histA[histPos] = a; histB[histPos] = b;
            histPos = (histPos + 1) % HISTORY;
        }
        if (seen || winLines >= LINES_PER_WINDOW) {
            if (dropped != UINT32_MAX) dropped++;
            return false;
        }
        winLines++;
        return true;
    }

    // once per frame: returns the number of dropped changes to summarise
    // (0 = nothing to write this frame)
    uint32_t tick()
    {
        if (++winFrames < WINDOW_FRAMES) return 0;
        winFrames = 0; winLines = 0;
        if (cooldown > 0) cooldown--;
        if (dropped == 0 || cooldown > 0) return 0;
        uint32_t n = dropped;
        dropped = 0;
        cooldown = SUMMARY_COOLDOWN;
        return n;
    }
};

#endif
