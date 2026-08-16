#include <stdlib.h>

#include "3dsrewind.h"
#include "3dsrewindring.h"
#include "3dssettings.h"
#include "3dssound.h"
#include "3dsmsu.h"
#include "3dsimpl_gpu.h"
#include "3dsui_notif.h"
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
#define REWIND_STEP_FRAMES      10    // pop cadence while the hotkey is held
#define REWIND_FORCE_GAP_FRAMES 120   // capture even without headroom after 2s
#define REWIND_TAP_MAX_FRAMES   12    // released within this = tap = timeline
#define REWIND_NOTIF_EMPTY_COOLDOWN_FRAMES  120
#define REWIND_NOTIF_STEP_COOLDOWN_FRAMES   30

// all state below is emu-thread only
static RewindRing s_ring;
static bool s_allocTried = false;
static bool s_msuDeferred = false;
static int  s_frameCounter = 0;
static int  s_framesSinceCapture = 0;
static int  s_notifCooldown = 0;
static int  s_heldFrames = 0;
static bool s_timelineRequested = false;
static bool s_timelineActive = false;
static uint32_t s_nowFrame = 0;        // emulated frames since ROM load

// timeline extras, allocated with the ring:
static uint8_t  *s_thumbPool = nullptr;    // slots * REWIND_THUMB_BYTES, RGB565 row-major
static uint8_t  *s_presentBuf = nullptr;   // the "present" snapshot while browsing
static uint32_t  s_presentLen = 0;

// The whole feature costs nothing until the user maps the hotkey: no
// ring allocation, no captures. This is the etapa-0 gating - everyone
// was paying 24MB + two ~450KB serializations per second for a feature
// whose hotkey ships unbound.
static bool rewind3dsHotkeyBound()
{
    const auto &hk = settings3DS.UseGlobalEmuControlKeys
        ? settings3DS.GlobalButtonHotkeys[HOTKEY_REWIND_HOLD]
        : settings3DS.ButtonHotkeys[HOTKEY_REWIND_HOLD];
    return hk.MappingBitmasks[0] != 0;
}

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
    s_heldFrames = 0;
    s_nowFrame = 0;
    s_timelineRequested = false;
    if (s_msuDeferred) {
        msu1_restore_deferred_cancel();   // never apply another game's snap
        s_msuDeferred = false;
    }
}

void rewind3dsFrameTick(bool rewindHeld, bool frameHadHeadroom)
{
    if (!settings3DS.isRomLoaded) return;
    if (snd3DS.generateSilence) return;   // SRAM autosave in flight
    if (!rewind3dsHotkeyBound()) return;  // unmapped = feature off = free

    if (!s_allocTried)
        rewind3dsAllocate();
    if (!s_ring.valid()) return;

    s_nowFrame++;
    s_frameCounter++;
    if (s_notifCooldown > 0) s_notifCooldown--;

    // One hotkey, two gestures: a tap (released within TAP_MAX frames)
    // opens the timeline; holding past it runs the quick rewind.
    if (rewindHeld) {
        s_heldFrames++;
    } else {
        if (s_heldFrames > 0 && s_heldFrames <= REWIND_TAP_MAX_FRAMES)
            s_timelineRequested = true;
        s_heldFrames = 0;
    }
    bool holdRewind = s_heldFrames > REWIND_TAP_MAX_FRAMES;

    // MSU-1 games: each rewind step would reopen/seek the audio track on
    // SD and hitch. While the hotkey is held the MSU restore is deferred
    // (music pauses); releasing applies the newest snapshot in one go.
    if (holdRewind) {
        rewind3dsMsuDeferBegin();
    } else if (!rewindHeld && s_msuDeferred && !s_timelineActive) {
        rewind3dsMsuDeferEnd();
    }

    if (holdRewind) {
        if (s_frameCounter < REWIND_STEP_FRAMES) return;
        s_frameCounter = 0;

        const uint8_t *data;
        uint32_t length;
        if (!s_ring.pop_peek(&data, &length)) {
            if (s_notifCooldown == 0) {
                notif3dsTrigger(Notif::Misc, Notif::Info, settings3DS.GameScreen,
                    900.0, "No more rewind data");
                s_notifCooldown = REWIND_NOTIF_EMPTY_COOLDOWN_FRAMES;
            }
            return;
        }

        // same fencing as a savestate load: the mixer must not touch the
        // core while it is being unfrozen (msu1_restore reopens files)
        snd3dsDrainMixing();
        bool ok = S9xUnfreezeGameMem(data, length);
        if (ok) {
            gpu3dsInitializeMode7Vertexes();
            if (!s_msuDeferred)
                msu3dsOnEvent(Msu1Event::SavestateLoaded);
        }
        snd3dsResumeMixing();

        if (ok) {
            s_ring.pop_commit();
            if (s_notifCooldown == 0) {
                notif3dsTrigger(Notif::Misc, Notif::Info, settings3DS.GameScreen,
                    600.0, "\x11\x11 Rewinding");
                s_notifCooldown = REWIND_NOTIF_STEP_COOLDOWN_FRAMES;
            }
        }
        return;
    }

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
