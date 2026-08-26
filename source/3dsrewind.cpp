#include <stdio.h>
#include <stdlib.h>

#include "3dsrewind.h"
#include "3dsrewinddeltaring.h"
#include "3dssettings.h"
#include "3dssound.h"
#include "3dsmsu.h"
#include "3dsimpl_gpu.h"
#include "3dsui_notif.h"
#include "3dslog.h"

#include "Snes9x/snes9x.h"
#include "Snes9x/snapshot.h"
#include "Snes9x/msu1.h"

// One uncompressed snapshot is ~450KB; 512KB keyframe slots leave
// headroom. Most captures store as page deltas against the newest
// keyframe (issue #37) - same RAM, many times the moments: roughly
// 1.5min on New 3DS (0.5s steps) and 1.2min on Old (2s steps), versus
// 24s/4s when every capture was a full state. Delta slots halve until
// the pools fit, so tight heaps degrade to fewer moments, never failure.
#define REWIND_SLOT_SIZE        (512 * 1024)
#define REWIND_DELTA_SLOT_SIZE  (96 * 1024)
#define REWIND_DELTA_PAGE       1024
#define REWIND_KF_NEW3DS        8
#define REWIND_KF_OLD3DS        4
#define REWIND_DELTAS_NEW3DS    160
#define REWIND_DELTAS_OLD3DS    32
#define REWIND_KF_INTERVAL      20    // a keyframe at least every 20 captures
#define REWIND_CAPTURE_FRAMES   (settings3DS.isNew3DS ? 30 : 120)
#define REWIND_FORCE_GAP_FRAMES (REWIND_CAPTURE_FRAMES * 4)

// all state below is emu-thread only
static RewindDeltaRing s_ring;
static uint8_t *s_readBuf = nullptr;   // delta reads decode here before unfreeze
static bool s_allocTried = false;
static bool s_msuDeferred = false;
static int  s_frameCounter = 0;
static int  s_framesSinceCapture = 0;
static int  s_holdFrames = 0;
static uint32_t s_statCaptures = 0;    // calibration: issue #37 pageSize/K
static uint32_t s_statDeltas = 0;
static uint64_t s_statDeltaBytes = 0;
static bool s_holdRequested = false;
static bool s_timelineRequested = false;
static bool s_timelineFromMenu = false;
static bool s_timelineActive = false;
static uint32_t s_nowFrame = 0;        // emulated frames since ROM load

// timeline extras, allocated with the ring:
static uint8_t  *s_thumbPool = nullptr;    // slots * REWIND_THUMB_BYTES, RGB565 row-major
static uint8_t  *s_presentBuf = nullptr;   // the "present" snapshot while browsing
static uint32_t  s_presentLen = 0;

static bool rewind3dsRestoreState(const uint8_t *data, uint32_t length);

// The feature is governed by the explicit Rewind setting (Emulator tab),
// NOT by the hotkey being mapped - the menu's Rewind action must work
// with no hotkey at all. Disabled = no ring allocation, no captures.

static void rewind3dsAllocate()
{
    s_allocTried = true;

    int kfSlots = settings3DS.isNew3DS ? REWIND_KF_NEW3DS : REWIND_KF_OLD3DS;
    int deltaSlots = settings3DS.isNew3DS ? REWIND_DELTAS_NEW3DS : REWIND_DELTAS_OLD3DS;

    while (deltaSlots >= 8) {
        int entryCount = kfSlots + deltaSlots;
        uint8_t *kfPool = (uint8_t *)malloc((size_t)kfSlots * REWIND_SLOT_SIZE);
        uint8_t *deltaPool = (uint8_t *)malloc((size_t)deltaSlots * REWIND_DELTA_SLOT_SIZE);
        RewindDeltaRing::Entry *entryBuf = (RewindDeltaRing::Entry *)
            malloc(entryCount * sizeof(RewindDeltaRing::Entry));
        s_thumbPool = (uint8_t *)malloc((size_t)entryCount * REWIND_THUMB_BYTES);
        s_presentBuf = (uint8_t *)malloc(REWIND_SLOT_SIZE);
        s_readBuf = (uint8_t *)malloc(REWIND_SLOT_SIZE);

        if (kfPool && deltaPool && entryBuf && s_thumbPool && s_presentBuf && s_readBuf) {
            memset(s_thumbPool, 0, (size_t)entryCount * REWIND_THUMB_BYTES);
            s_ring.init(kfPool, kfSlots, REWIND_SLOT_SIZE,
                        deltaPool, deltaSlots, REWIND_DELTA_SLOT_SIZE,
                        entryBuf, entryCount,
                        REWIND_DELTA_PAGE, REWIND_KF_INTERVAL);
            log3dsWrite("[rewind] delta ring ready: %d keyframes + %d delta slots"
                " (~%ds of gameplay at best)",
                kfSlots, deltaSlots, entryCount * REWIND_CAPTURE_FRAMES / 60);
            return;
        }

        free(kfPool); free(deltaPool); free(entryBuf);
        free(s_thumbPool); s_thumbPool = nullptr;
        free(s_presentBuf); s_presentBuf = nullptr;
        free(s_readBuf); s_readBuf = nullptr;
        deltaSlots /= 2;
    }
    log3dsWrite("[rewind] not enough memory for the delta ring - disabled");
}

// 100x60 RGB565 miniature of the finished game screen, decimated straight
// from the (column-major, BGR8) framebuffer - the composited image, so it
// looks like what the player saw. One-frame lag is irrelevant for a thumb.
static void rewind3dsCaptureThumb(uint8_t *dst)
{
    u8 *fb = gfxGetFramebuffer(settings3DS.GameScreen, GFX_LEFT, NULL, NULL);
    if (fb == NULL) { memset(dst, 0, REWIND_THUMB_BYTES); return; }

    int fbWidth = (settings3DS.GameScreen == GFX_TOP) ? 400 : 320;
    if (settings3DS.GameScreen == GFX_TOP && gfxIsWide()) { fbWidth = 800; }

    // proportional sampling: integer steps (fbWidth / W) truncate when the
    // thumb size does not divide the screen - 150 wide sampled only the
    // top-left 75% of a 400px frame (field report 16/08, cropped thumbs)
    uint16_t *out = (uint16_t *)dst;
    for (int ty = 0; ty < REWIND_THUMB_H; ty++) {
        for (int tx = 0; tx < REWIND_THUMB_W; tx++) {
            int x = tx * fbWidth / REWIND_THUMB_W;
            int y = ty * 240 / REWIND_THUMB_H;
            u8 *px = fb + (x * 240 + (239 - y)) * 3;   // B,G,R
            out[ty * REWIND_THUMB_W + tx] = (uint16_t)(
                ((px[2] & 0xF8) << 8) | ((px[1] & 0xFC) << 3) | (px[0] >> 3));
        }
    }
}

// --- timeline support (3dsrewindui.cpp drives these; emu thread only) ------

bool rewind3dsTimelineActive() { return s_timelineActive; }
void rewind3dsSetTimelineActive(bool active) { s_timelineActive = active; }

bool rewind3dsTakeTimelineRequest()
{
    bool req = s_timelineRequested;
    s_timelineRequested = false;
    return req;
}

int rewind3dsCaptureIntervalFrames()
{
    return REWIND_CAPTURE_FRAMES;
}

int rewind3dsEmulatedFps()
{
    return Settings.PAL ? 50 : 60;
}

bool rewind3dsTakeHoldRequest()
{
    bool req = s_holdRequested;
    s_holdRequested = false;
    return req;
}

// one step of the live walk: restore the newest stored moment under the
// mixer barrier and consume it; false when history is exhausted
bool rewind3dsHoldStepBack()
{
    if (!s_ring.valid() || s_ring.count == 0 || s_readBuf == nullptr) return false;
    uint32_t tag = 0;
    uint32_t len = s_ring.read_at(0, s_readBuf, REWIND_SLOT_SIZE);
    if (len == 0 || !s_ring.tag_at(0, &tag)) return false;
    LightLock_Lock(&snd3DS.snesAccessLock);
    bool restored = rewind3dsRestoreState(s_readBuf, len);
    LightLock_Unlock(&snd3DS.snesAccessLock);
    if (!restored) return false;
    s_nowFrame = tag;
    s_ring.pop_newest();
    return true;
}

// entry via the Emulator-menu action: B must return to the menu, not the game
void rewind3dsRequestTimelineFromMenu()
{
    s_timelineRequested = true;
    s_timelineFromMenu = true;
}

bool rewind3dsTimelineFromMenu() { return s_timelineFromMenu; }

int rewind3dsCount() { return s_ring.valid() ? s_ring.count : 0; }
uint32_t rewind3dsNowFrame() { return s_nowFrame; }

bool rewind3dsPeekInfo(int back, uint32_t *frameTag)
{
    return s_ring.valid() && s_ring.tag_at(back, frameTag);
}

const uint8_t *rewind3dsThumb(int back)
{
    if (!s_ring.valid() || back < 0 || back >= s_ring.count) return nullptr;
    return s_thumbPool + (size_t)s_ring.entry_pos(back) * REWIND_THUMB_BYTES;
}

bool rewind3dsCapturePresent()
{
    if (s_presentBuf == nullptr) return false;
    uint32 length = 0;
    LightLock_Lock(&snd3DS.snesAccessLock);
    bool ok = S9xFreezeGameMem(s_presentBuf, REWIND_SLOT_SIZE, &length);
    LightLock_Unlock(&snd3DS.snesAccessLock);
    s_presentLen = ok ? length : 0;
    return ok;
}

static bool rewind3dsRestoreState(const uint8_t *data, uint32_t length)
{
    bool ok = S9xUnfreezeGameMem(data, length);
    if (ok) { gpu3dsInitializeMode7Vertexes(); }
    return ok;
}

bool rewind3dsRestorePresent()
{
    return s_presentLen != 0 && rewind3dsRestoreState(s_presentBuf, s_presentLen);
}

bool rewind3dsRestoreAt(int back)
{
    if (!s_ring.valid() || s_readBuf == nullptr) return false;
    uint32_t len = s_ring.read_at(back, s_readBuf, REWIND_SLOT_SIZE);
    return len != 0 && rewind3dsRestoreState(s_readBuf, len);
}

void rewind3dsRollbackTo(int back)
{
    if (s_ring.valid()) s_ring.rollback_to(back);
}

// MSU-1 stays latched and silent while the timeline browses; the release
// applies the last restored snapshot's audio in one go
void rewind3dsMsuDeferBegin()
{
    if (!s_msuDeferred && Settings.MSU1) {
        msu1_set_restore_deferred(true);
        s_msuDeferred = true;
    }
}

void rewind3dsMsuDeferEnd()
{
    if (s_msuDeferred) {
        snd3dsDrainMixing();
        msu1_set_restore_deferred(false);
        snd3dsResumeMixing();
        msu3dsOnEvent(Msu1Event::SavestateLoaded);
        s_msuDeferred = false;
    }
}

void rewind3dsReset()
{
    if (s_ring.valid())
        s_ring.clear();
    s_frameCounter = 0;
    s_holdFrames = 0;
    s_holdRequested = false;
    s_nowFrame = 0;
    s_timelineRequested = false;
    if (s_msuDeferred) {
        msu1_restore_deferred_cancel();   // never apply another game's snap
        s_msuDeferred = false;
    }
}

void rewind3dsFrameTick(bool rewindHeld, int frameLoadPercent)
{
    if (!settings3DS.isRomLoaded) return;
    if (snd3DS.generateSilence) return;   // SRAM autosave in flight
    if (!settings3DS.RewindEnabled) return;   // disabled = hotkey dead too

    // Tap/hold hotkey - always combined (user design 17/08): holding
    // past half a second enters the live-rewind modal (game frozen,
    // dimmed, countdown on release - rewind3dsHoldShow); a short tap
    // opens the timeline on release.
    if (rewindHeld) {
        s_holdFrames++;
        if (s_holdFrames == 30)
            s_holdRequested = true;
    } else {
        if (s_holdFrames > 0 && s_holdFrames < 30) {
            s_timelineRequested = true;
            s_timelineFromMenu = false;
        }
        s_holdFrames = 0;
    }

    if (!s_allocTried)
        rewind3dsAllocate();
    if (!s_ring.valid()) return;

    s_nowFrame++;
    s_frameCounter++;

    // The hold gesture is gone (docs/rewind-v2-spec.md): pressing the
    // Rewind hotkey opens the timeline, period.
    // Graduated capture patience (user design, 16/08): a due capture
    // first insists on a really idle frame; the idleness bar lowers as
    // the wait grows (50% -> 75% -> 90% of the frame budget), and at the
    // configured limit (Capture Patience) it captures regardless - fewer
    // perfect landing spots instead of a dried-up history.
    s_framesSinceCapture++;
    if (s_frameCounter >= REWIND_CAPTURE_FRAMES) {
        static const int patienceFrames[4] = { 60, 120, 240, 480 };
        int waitLimit = patienceFrames[settings3DS.RewindMaxWait & 3];
        int waited = s_frameCounter - REWIND_CAPTURE_FRAMES;

        int loadBar;
        if (waited >= waitLimit)              loadBar = 1000;   // force
        else if (waited >= waitLimit * 2 / 3) loadBar = 90;
        else if (waited >= waitLimit / 3)     loadBar = 75;
        else                                  loadBar = 50;

        if (frameLoadPercent <= loadBar) {
            s_frameCounter = 0;
            s_framesSinceCapture = 0;
            uint32 length = 0;   // snes9x's uint32 (int-based) != uint32_t here

            // Hold the mixer barrier for the freeze: the canonicalize+
            // restore inside S9xFreezeGameMem must never be visible to a
            // concurrent mix pass, or a channel loses a note (audible at
            // 268MHz, where the freeze spans mixer callbacks). A blocked
            // mixer just waits a few ms on queued NDSP buffers - unlike
            // snd3dsDrainMixing, which would inject silence every capture.
            u64 capStartTick = svcGetSystemTick();
            uint8_t *staging = s_ring.push_ptr();
            bool ok = false;
            if (staging != nullptr) {
                LightLock_Lock(&snd3DS.snesAccessLock);
                ok = S9xFreezeGameMem(staging, REWIND_SLOT_SIZE, &length);
                LightLock_Unlock(&snd3DS.snesAccessLock);
            }
            if (ok) {
                s_ring.push_commit(length, s_nowFrame);

                // A capture that outruns the frame budget is a visible
                // stutter - name it in the log so field reports can tell
                // capture spikes from SRAM/SD writes.
                uint32_t capMs = (uint32_t)((svcGetSystemTick() - capStartTick) / 268123);
                if (capMs >= 8)
                    log3dsWrite("[rewind] capture slow: %ums (%s, %uKB)", capMs,
                        s_ring.at(0).kind == RewindDeltaRing::KIND_DELTA ? "delta" : "keyframe",
                        (unsigned)(s_ring.at(0).len / 1024));

                // calibration feed (log enabled only): real delta sizes
                // and promotion rate decide pageSize/K for issue #37
                s_statCaptures++;
                if (s_ring.at(0).kind == RewindDeltaRing::KIND_DELTA) {
                    s_statDeltas++;
                    s_statDeltaBytes += s_ring.at(0).len;
                }
                if ((s_statCaptures % 64) == 0) {
                    uint32_t first = 0, last = 0;
                    s_ring.tag_at(0, &first);
                    s_ring.tag_at(s_ring.count - 1, &last);
                    log3dsWrite("[rewind] calib: %u caps, %u%% delta, avg %uKB, window %us (%d entries)",
                        (unsigned)s_statCaptures,
                        (unsigned)(s_statDeltas * 100 / s_statCaptures),
                        (unsigned)(s_statDeltas ? s_statDeltaBytes / s_statDeltas / 1024 : 0),
                        (unsigned)((first - last) / (uint32_t)rewind3dsEmulatedFps()),
                        s_ring.count);
                }
                rewind3dsCaptureThumb(
                    s_thumbPool + (size_t)s_ring.entry_pos(0) * REWIND_THUMB_BYTES);

                // Max History ceiling (menu): drop whole oldest groups
                if (settings3DS.RewindMaxWindow < 2) {
                    int seconds = settings3DS.RewindMaxWindow == 0 ? 30 : 60;
                    s_ring.trim_to(seconds * 60 / REWIND_CAPTURE_FRAMES);
                }
            }
        }
    }
}
