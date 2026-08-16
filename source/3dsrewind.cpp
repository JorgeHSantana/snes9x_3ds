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
#define REWIND_NOTIF_EMPTY_COOLDOWN_FRAMES  120
#define REWIND_NOTIF_STEP_COOLDOWN_FRAMES   30

// all state below is emu-thread only
static RewindRing s_ring;
static bool s_allocTried = false;
static bool s_msuDeferred = false;
static int  s_frameCounter = 0;
static int  s_framesSinceCapture = 0;
static int  s_notifCooldown = 0;

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
            s_ring.init(pool, lens, slots, REWIND_SLOT_SIZE);
            log3dsWrite("[rewind] ring ready: %d slots (%d KB), ~%ds of gameplay",
                slots, slots * (REWIND_SLOT_SIZE / 1024),
                slots * REWIND_CAPTURE_FRAMES / 60);
            return;
        }
        slots /= 2;
    }
    log3dsWrite("[rewind] not enough memory for a snapshot ring - disabled");
}

void rewind3dsReset()
{
    if (s_ring.valid())
        s_ring.clear();
    s_frameCounter = 0;
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

    s_frameCounter++;
    if (s_notifCooldown > 0) s_notifCooldown--;

    // MSU-1 games: each rewind step would reopen/seek the audio track on
    // SD and hitch. While the hotkey is held the MSU restore is deferred
    // (music pauses); releasing applies the newest snapshot in one go.
    if (rewindHeld && !s_msuDeferred && Settings.MSU1) {
        msu1_set_restore_deferred(true);
        s_msuDeferred = true;
    } else if (!rewindHeld && s_msuDeferred) {
        snd3dsDrainMixing();
        msu1_set_restore_deferred(false);
        snd3dsResumeMixing();
        msu3dsOnEvent(Msu1Event::SavestateLoaded);
        s_msuDeferred = false;
    }

    if (rewindHeld) {
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

            if (ok)
                s_ring.push_commit(length);
        }
    }
}
