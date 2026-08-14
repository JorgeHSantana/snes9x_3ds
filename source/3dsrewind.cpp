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

// One uncompressed snapshot is ~450KB; 512KB slots leave headroom.
// New 3DS affords ~24 seconds of rewind, Old 3DS ~4 seconds - the pool
// allocation halves until it fits, so tight heaps degrade gracefully.
#define REWIND_SLOT_SIZE        (512 * 1024)
#define REWIND_SLOTS_NEW3DS     48
#define REWIND_SLOTS_OLD3DS     8
#define REWIND_CAPTURE_FRAMES   30   // one snapshot every half second
#define REWIND_STEP_FRAMES      10   // pop cadence while the hotkey is held

static RewindRing s_ring;
static bool s_allocTried = false;
static int  s_frameCounter = 0;
static int  s_notifCooldown = 0;

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
}

void rewind3dsFrameTick(bool rewindHeld)
{
    if (!settings3DS.isRomLoaded) return;
    if (snd3DS.generateSilence) return;   // SRAM autosave in flight

    if (!s_allocTried)
        rewind3dsAllocate();
    if (!s_ring.valid()) return;

    s_frameCounter++;
    if (s_notifCooldown > 0) s_notifCooldown--;

    if (rewindHeld) {
        if (s_frameCounter < REWIND_STEP_FRAMES) return;
        s_frameCounter = 0;

        const uint8_t *data;
        uint32_t length;
        if (!s_ring.popPeek(&data, &length)) {
            if (s_notifCooldown == 0) {
                notif3dsTrigger(Notif::Misc, Notif::Info, settings3DS.GameScreen,
                    900.0, "No more rewind data");
                s_notifCooldown = 120;
            }
            return;
        }

        // same fencing as a savestate load: the mixer must not touch the
        // core while it is being unfrozen (msu1_restore reopens files)
        snd3dsDrainMixing();
        bool ok = S9xUnfreezeGameMem(data, length);
        if (ok) {
            gpu3dsInitializeMode7Vertexes();
            msu3dsOnEvent(Msu1Event::SavestateLoaded);
        }
        snd3dsResumeMixing();

        if (ok) {
            s_ring.popCommit();
            if (s_notifCooldown == 0) {
                notif3dsTrigger(Notif::Misc, Notif::Info, settings3DS.GameScreen,
                    600.0, "\x11\x11 Rewinding");
                s_notifCooldown = 30;
            }
        }
        return;
    }

    if (s_frameCounter >= REWIND_CAPTURE_FRAMES) {
        s_frameCounter = 0;
        uint32 length = 0;   // snes9x's uint32 (int-based) != uint32_t here
        if (S9xFreezeGameMem(s_ring.pushPtr(), s_ring.slotSize, &length))
            s_ring.pushCommit(length);
    }
}
