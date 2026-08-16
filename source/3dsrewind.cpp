#include <stdlib.h>

#include "3dsrewind.h"
#include "3dsrewindring.h"
#include "3dsrewindtape.h"
#include "3dsrewindmeter.h"
#include "3dssettings.h"
#include "3dssound.h"
#include "3dsmsu.h"
#include "3dsimpl_gpu.h"
#include "3dslog.h"

#include "Snes9x/snes9x.h"
#include "Snes9x/snapshot.h"
#include "Snes9x/msu1.h"

// One uncompressed snapshot is ~450KB; 512KB slots leave headroom.
// New 3DS affords ~24 seconds of rewind, Old 3DS ~4 seconds - the pool
// allocation halves until it fits, so tight heaps degrade gracefully.
#define REWIND_SLOT_SIZE        (512 * 1024)
#define REWIND_SLOTS_NEW3DS     48
#define REWIND_SLOTS_OLD3DS     8
#define REWIND_CAPTURE_FRAMES   30    // one snapshot every half second
#define REWIND_FORCE_GAP_FRAMES 120   // capture even without headroom after 2s

// Input tape (v2 degrau 2): 10 minutes of pads (4B/frame) plus MSU-1
// status reads - ~176KB total, recording only, nothing replays it yet.
#define REWIND_TAPE_FRAMES      (10 * 60 * 60)
#define REWIND_TAPE_MSU_EVENTS  4096

// all state below is emu-thread only
static RewindRing s_ring;
static bool s_allocTried = false;
static bool s_msuDeferred = false;
static int  s_frameCounter = 0;
static int  s_framesSinceCapture = 0;
static bool s_wasHeld = false;
static bool s_timelineRequested = false;
static bool s_timelineFromMenu = false;
static bool s_timelineActive = false;
static uint32_t s_nowFrame = 0;        // emulated frames since ROM load

// timeline extras, allocated with the ring:
static uint8_t  *s_thumbPool = nullptr;    // slots * REWIND_THUMB_BYTES, RGB565 row-major
static uint8_t  *s_presentBuf = nullptr;   // the "present" snapshot while browsing
static uint32_t  s_presentLen = 0;

// v2 recording (emu thread only): tape frame N = the frame that set
// s_nowFrame to N; MSU events tag the frame being executed
static RewindTape  s_tape;
static RewindMeter s_meter;
static uint32_t    s_lastPad = 0;

// The feature is governed by the explicit Rewind setting (Emulator tab),
// NOT by the hotkey being mapped - the menu's Rewind action must work
// with no hotkey at all. Disabled = no ring allocation, no captures.

static void rewind3dsAllocate()
{
    s_allocTried = true;

    int slots = settings3DS.isNew3DS ? REWIND_SLOTS_NEW3DS : REWIND_SLOTS_OLD3DS;
    while (slots >= 2) {
        uint8_t *pool = (uint8_t *)malloc((size_t)slots * REWIND_SLOT_SIZE);
        if (pool) {
            uint32_t *lens = (uint32_t *)malloc(slots * sizeof(uint32_t));
            if (!lens) { free(pool); break; }
            uint32_t *tagsBuf = (uint32_t *)malloc(slots * sizeof(uint32_t));
            if (!tagsBuf) { free(lens); free(pool); break; }
            s_thumbPool = (uint8_t *)malloc((size_t)slots * REWIND_THUMB_BYTES);
            if (!s_thumbPool) { free(tagsBuf); free(lens); free(pool); break; }
            s_presentBuf = (uint8_t *)malloc(REWIND_SLOT_SIZE);
            if (!s_presentBuf) { free(s_thumbPool); s_thumbPool = nullptr;
                                 free(tagsBuf); free(lens); free(pool); break; }
            memset(s_thumbPool, 0, (size_t)slots * REWIND_THUMB_BYTES);
            s_ring.init(pool, lens, tagsBuf, slots, REWIND_SLOT_SIZE);
            log3dsWrite("[rewind] ring ready: %d slots (%d KB), ~%ds of gameplay",
                slots, slots * (REWIND_SLOT_SIZE / 1024),
                slots * REWIND_CAPTURE_FRAMES / 60);

            // v2 tape rides along; losing it degrades nothing visible yet
            uint32_t *padBuf = (uint32_t *)malloc(REWIND_TAPE_FRAMES * sizeof(uint32_t));
            RewindTape::MsuRead *msuBuf = (RewindTape::MsuRead *)
                malloc(REWIND_TAPE_MSU_EVENTS * sizeof(RewindTape::MsuRead));
            if (padBuf && msuBuf) {
                s_tape.init(padBuf, REWIND_TAPE_FRAMES, msuBuf, REWIND_TAPE_MSU_EVENTS);
                s_tape.clear(s_nowFrame + 1);
                log3dsWrite("[rewind] tape ready: %d frames, %d msu events",
                    REWIND_TAPE_FRAMES, REWIND_TAPE_MSU_EVENTS);
            } else {
                free(padBuf); free(msuBuf);
                log3dsWrite("[rewind] no memory for the input tape - recording without it");
            }
            return;
        }
        slots /= 2;
    }
    log3dsWrite("[rewind] not enough memory for a snapshot ring - disabled");
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
    int xStep = fbWidth / REWIND_THUMB_W;
    int yStep = 240 / REWIND_THUMB_H;

    uint16_t *out = (uint16_t *)dst;
    for (int ty = 0; ty < REWIND_THUMB_H; ty++) {
        for (int tx = 0; tx < REWIND_THUMB_W; tx++) {
            int x = tx * xStep;
            int y = ty * yStep;
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
    const uint8_t *data; uint32_t len;
    return s_ring.valid() && s_ring.peek_at(back, &data, &len, frameTag);
}

const uint8_t *rewind3dsThumb(int back)
{
    if (!s_ring.valid() || back < 0 || back >= s_ring.count) return nullptr;
    return s_thumbPool + (size_t)s_ring.slot_at(back) * REWIND_THUMB_BYTES;
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
    const uint8_t *data; uint32_t len, tag;
    if (!s_ring.valid() || !s_ring.peek_at(back, &data, &len, &tag)) return false;
    return rewind3dsRestoreState(data, len);
}

void rewind3dsRollbackTo(int back)
{
    if (!s_ring.valid()) return;

    // the chosen moment becomes the present: the frame counter rewinds to
    // its tag so future captures and tape frames continue from there
    uint32_t tag = 0;
    const uint8_t *data; uint32_t len;
    if (s_ring.peek_at(back, &data, &len, &tag)) {
        s_nowFrame = tag;
        if (s_tape.valid())
            s_tape.truncate_to(tag);
    }
    s_ring.rollback_to(back);
}

// --- v2 recording hooks -----------------------------------------------------

void rewind3dsNotePad(uint32_t pad)
{
    s_lastPad = pad;
}

void rewind3dsNoteMsuStatus(uint8_t value)
{
    if (!s_tape.valid() || s_timelineActive || !settings3DS.RewindEnabled) return;
    s_tape.note_msu(0, value);
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
    s_wasHeld = false;
    s_nowFrame = 0;
    s_timelineRequested = false;
    if (s_tape.valid())
        s_tape.clear(1);   // first executed frame will be frame 1
    s_meter.reset();
    s_lastPad = 0;
    if (s_msuDeferred) {
        msu1_restore_deferred_cancel();   // never apply another game's snap
        s_msuDeferred = false;
    }
}

void rewind3dsFrameTick(bool rewindHeld, bool frameHadHeadroom)
{
    if (!settings3DS.isRomLoaded) return;
    if (snd3DS.generateSilence) return;   // SRAM autosave in flight
    if (!settings3DS.RewindEnabled) return;   // disabled = free

    if (!s_allocTried)
        rewind3dsAllocate();
    if (!s_ring.valid()) return;

    s_nowFrame++;
    s_frameCounter++;

    // commit the executed frame's pad to the tape (v2 degrau 2: recording
    // only - the ring below still drives every visible behavior)
    if (s_tape.valid())
        s_tape.push(s_lastPad);

    // The hold gesture is gone (docs/rewind-v2-spec.md): pressing the
    // Rewind hotkey opens the timeline, period.
    if (rewindHeld && !s_wasHeld) {
        s_timelineRequested = true;
        s_timelineFromMenu = false;
    }
    s_wasHeld = rewindHeld;

    // Adaptive capture: prefer frames that finished with vsync headroom,
    // so the serialization lands where the CPU would have idled. A game
    // with no slack still gets a forced snapshot every 2s - fewer rewind
    // points instead of dropped frames.
    s_framesSinceCapture++;
    if (s_frameCounter >= REWIND_CAPTURE_FRAMES) {
        bool force = s_framesSinceCapture >= REWIND_FORCE_GAP_FRAMES;
        if (frameHadHeadroom || force) {
            s_frameCounter = 0;
            s_framesSinceCapture = 0;
            uint32 length = 0;   // snes9x's uint32 (int-based) != uint32_t here

            // Hold the mixer barrier for the freeze: the canonicalize+
            // restore inside S9xFreezeGameMem must never be visible to a
            // concurrent mix pass, or a channel loses a note (audible at
            // 268MHz, where the freeze spans mixer callbacks). A blocked
            // mixer just waits a few ms on queued NDSP buffers - unlike
            // snd3dsDrainMixing, which would inject silence every capture.
            LightLock_Lock(&snd3DS.snesAccessLock);
            bool ok = S9xFreezeGameMem(s_ring.push_ptr(), s_ring.slotSize, &length);
            LightLock_Unlock(&snd3DS.snesAccessLock);

            if (ok) {
                s_ring.push_commit(length, s_nowFrame);
                rewind3dsCaptureThumb(
                    s_thumbPool + (size_t)s_ring.slot_at(0) * REWIND_THUMB_BYTES);
            }
        }
    }
}
