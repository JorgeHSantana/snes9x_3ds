#include <stdlib.h>

#include "3dsrewind.h"
#include "3dsrewindring.h"
#include "3dsrewindtape.h"
#include "3dsrewindmeter.h"
#include "3dsrewindticks.h"
#include "3dssettings.h"
#include "3dssound.h"
#include "3dsimpl.h"
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

// Input tape (v2): 10 minutes of pads (4B/frame) plus MSU-1 status reads.
#define REWIND_TAPE_FRAMES      (10 * 60 * 60)
#define REWIND_TAPE_MSU_EVENTS  4096

// v2 window engine (degrau 3, New 3DS only): anchors + tick-states filled
// by replaying the tape. Replaces the snapshot ring there; Old 3DS keeps
// the ring (docs/rewind-v2-spec.md: same UI, different moment provider).
#define REWIND_ANCHOR_SLOTS      16
#define REWIND_ANCHOR_GAP_START  600    // 10s, adapts once replay speed is known
#define REWIND_ANCHOR_GAP_MIN    300    // 5s
#define REWIND_ANCHOR_GAP_MAX    1800   // 30s
#define REWIND_ANCHOR_LATENCY    120    // tolerated first-jump latency (2s)
#define REWIND_TICK_FRAMES       60     // one browsable moment per second
#define REWIND_TICK_SLOTS        24
#define REWIND_V2_THUMB_SLOTS    340    // ~5.6min of 1s thumbs (4MB)
#define REWIND_PREFETCH_BUDGET_MS 8     // background replay per UI frame

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

// v2 window engine (all emu thread). s_v2 true = this console browses
// tape+anchors; false = the legacy snapshot ring provider.
static bool        s_v2 = false;
static RewindRing  s_anchors;
static int         s_anchorGapFrames = REWIND_ANCHOR_GAP_START;
static int         s_framesSinceAnchor = 0;
static RewindTicks s_ticks;
static uint8_t    *s_tickPool = nullptr;
static uint32_t    s_tickLens[REWIND_TICK_SLOTS];
static uint8_t    *s_thumb2Pool = nullptr;
static uint32_t   *s_thumb2Tags = nullptr;   // absolute frame per slot, 0 = empty

// replay driver: while active, S9xReadJoypad and the MSU status port are
// fed from the tape instead of live input/state
static bool        s_replayActive = false;
static uint32_t    s_replayFrame = 0;        // frame being re-executed
static int         s_replayMsuIdx = 0;

// background prefetch: one tick materializes across many UI frames
static uint32_t    s_prefetchTarget = 0;     // 0 = idle

// The feature is governed by the explicit Rewind setting (Emulator tab),
// NOT by the hotkey being mapped - the menu's Rewind action must work
// with no hotkey at all. Disabled = no ring allocation, no captures.

// v2 window engine memory (New 3DS): anchors + tick slots + 1s thumbs +
// present buffer + tape, ~25MB total - the same budget the old ring used.
// Any failure frees everything and falls back to the ring provider.
static bool rewind3dsAllocateV2()
{
    uint8_t  *anchorPool = (uint8_t *)malloc((size_t)REWIND_ANCHOR_SLOTS * REWIND_SLOT_SIZE);
    uint32_t *anchorLens = (uint32_t *)malloc(REWIND_ANCHOR_SLOTS * sizeof(uint32_t));
    uint32_t *anchorTags = (uint32_t *)malloc(REWIND_ANCHOR_SLOTS * sizeof(uint32_t));
    s_tickPool = (uint8_t *)malloc((size_t)REWIND_TICK_SLOTS * REWIND_SLOT_SIZE);
    static uint32_t tickFrames[REWIND_TICK_SLOTS];
    s_thumb2Pool = (uint8_t *)malloc((size_t)REWIND_V2_THUMB_SLOTS * REWIND_THUMB_BYTES);
    s_thumb2Tags = (uint32_t *)malloc(REWIND_V2_THUMB_SLOTS * sizeof(uint32_t));
    s_presentBuf = (uint8_t *)malloc(REWIND_SLOT_SIZE);
    uint32_t *padBuf = (uint32_t *)malloc(REWIND_TAPE_FRAMES * sizeof(uint32_t));
    RewindTape::MsuRead *msuBuf = (RewindTape::MsuRead *)
        malloc(REWIND_TAPE_MSU_EVENTS * sizeof(RewindTape::MsuRead));

    if (!anchorPool || !anchorLens || !anchorTags || !s_tickPool
            || !s_thumb2Pool || !s_thumb2Tags || !s_presentBuf || !padBuf || !msuBuf) {
        free(anchorPool); free(anchorLens); free(anchorTags);
        free(s_tickPool); s_tickPool = nullptr;
        free(s_thumb2Pool); s_thumb2Pool = nullptr;
        free(s_thumb2Tags); s_thumb2Tags = nullptr;
        free(s_presentBuf); s_presentBuf = nullptr;
        free(padBuf); free(msuBuf);
        return false;
    }

    s_anchors.init(anchorPool, anchorLens, anchorTags, REWIND_ANCHOR_SLOTS, REWIND_SLOT_SIZE);
    s_ticks.init(tickFrames, REWIND_TICK_SLOTS);
    memset(s_thumb2Tags, 0, REWIND_V2_THUMB_SLOTS * sizeof(uint32_t));
    s_tape.init(padBuf, REWIND_TAPE_FRAMES, msuBuf, REWIND_TAPE_MSU_EVENTS);
    s_tape.clear(s_nowFrame + 1);
    log3dsWrite("[rewind] v2 engine ready: %d anchors, %d tick slots, %d thumbs, %d tape frames",
        REWIND_ANCHOR_SLOTS, REWIND_TICK_SLOTS, REWIND_V2_THUMB_SLOTS, REWIND_TAPE_FRAMES);
    return true;
}

static void rewind3dsAllocate()
{
    s_allocTried = true;

    if (settings3DS.isNew3DS) {
        s_v2 = rewind3dsAllocateV2();
        if (s_v2) return;
        log3dsWrite("[rewind] v2 allocation failed - falling back to the snapshot ring");
    }

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

// --- v2 provider: browsable moments are 1s ticks over the anchor horizon ----

// ticks sit on absolute multiples of REWIND_TICK_FRAMES so the 1s thumbs
// captured during play line up with them; back 0 = most recent tick
static uint32_t v2TickFrame(int back)
{
    return (s_nowFrame / REWIND_TICK_FRAMES - (uint32_t)back) * REWIND_TICK_FRAMES;
}

// oldest browsable frame: the oldest anchor whose forward tape is intact
static bool v2HorizonStart(uint32_t *start)
{
    for (int b = s_anchors.count - 1; b >= 0; b--) {
        const uint8_t *data = nullptr; uint32_t len = 0, tag = 0;
        if (!s_anchors.peek_at(b, &data, &len, &tag)) continue;
        if (tag + 1 >= s_tape.validFrom) { *start = tag; return true; }
    }
    return false;
}

int rewind3dsCount()
{
    if (s_v2) {
        uint32_t start;
        if (!v2HorizonStart(&start)) return 0;
        uint32_t newest = v2TickFrame(0);
        if (newest < start || newest == 0) return 0;
        return (int)((newest - start) / REWIND_TICK_FRAMES) + 1;
    }
    return s_ring.valid() ? s_ring.count : 0;
}

uint32_t rewind3dsNowFrame() { return s_nowFrame; }

bool rewind3dsPeekInfo(int back, uint32_t *frameTag)
{
    if (s_v2) {
        if (back < 0 || back >= rewind3dsCount()) return false;
        *frameTag = v2TickFrame(back);
        return true;
    }
    const uint8_t *data; uint32_t len;
    return s_ring.valid() && s_ring.peek_at(back, &data, &len, frameTag);
}

const uint8_t *rewind3dsThumb(int back)
{
    if (s_v2) {
        if (back < 0 || back >= rewind3dsCount()) return nullptr;
        uint32_t f = v2TickFrame(back);
        int slot = (f / REWIND_TICK_FRAMES) % REWIND_V2_THUMB_SLOTS;
        if (s_thumb2Tags[slot] != f) return nullptr;
        return s_thumb2Pool + (size_t)slot * REWIND_THUMB_BYTES;
    }
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
    s_prefetchTarget = 0;
    s_replayActive = false;
    return s_presentLen != 0 && rewind3dsRestoreState(s_presentBuf, s_presentLen);
}

// --- v2 replay driver -------------------------------------------------------

bool rewind3dsReplayActive() { return s_replayActive; }

uint32_t rewind3dsReplayPad()
{
    uint32_t pad = 0;
    s_tape.pad_at(s_replayFrame, &pad);
    return pad;
}

// feeds the recorded MSU status values back to the frame re-executing them
bool rewind3dsMsuStatusOverride(uint8_t *value)
{
    if (!s_replayActive) return false;
    while (s_replayMsuIdx < s_tape.msu_event_count()
            && s_tape.msu_event(s_replayMsuIdx).frame < s_replayFrame)
        s_replayMsuIdx++;
    if (s_replayMsuIdx < s_tape.msu_event_count()
            && s_tape.msu_event(s_replayMsuIdx).frame == s_replayFrame) {
        *value = s_tape.msu_event(s_replayMsuIdx).value;
        s_replayMsuIdx++;
        return true;
    }
    return false;   // nothing recorded for this read: live value stands
}

static void rewind3dsReplayBegin(uint32_t sourceFrame)
{
    s_replayActive = true;
    s_replayFrame = sourceFrame;
    s_replayMsuIdx = 0;
}

// re-executes frames until targetFrame or the tick budget runs out
// (0 = unbounded); every run feeds the replay-speed meter
static bool rewind3dsReplayRun(uint32_t targetFrame, uint64_t budgetTicks)
{
    uint64_t t0 = svcGetSystemTick();
    uint32_t frames = 0;
    while (s_replayFrame < targetFrame) {
        s_replayFrame++;
        impl3dsRunOneFrame(false, true);
        frames++;
        if (budgetTicks != 0 && svcGetSystemTick() - t0 >= budgetTicks)
            break;
    }
    s_meter.note(frames, svcGetSystemTick() - t0,
        (uint64_t)(CPU_TICKS_PER_MSEC * 1000.0 / 60.0));
    return s_replayFrame >= targetFrame;
}

static void rewind3dsReplayEnd() { s_replayActive = false; }

// freeze the live core state (at 'frame') into a tick slot; eviction
// keeps the neighbourhood of cursorFrame
static void v2FreezeTick(uint32_t frame, uint32_t cursorFrame)
{
    int slot = s_ticks.alloc(frame, cursorFrame);
    if (slot < 0) return;
    uint32 length = 0;
    LightLock_Lock(&snd3DS.snesAccessLock);
    bool ok = S9xFreezeGameMem(s_tickPool + (size_t)slot * REWIND_SLOT_SIZE,
        REWIND_SLOT_SIZE, &length);
    LightLock_Unlock(&snd3DS.snesAccessLock);
    if (!ok) { s_ticks.frames[slot] = RewindTicks::NO_FRAME; return; }
    s_tickLens[slot] = length;
}

// nearest usable state at-or-below 'frame' (resident tick or anchor),
// loaded into the core; *sourceFrame tells where the replay starts
static bool v2LoadSource(uint32_t frame, uint32_t *sourceFrame)
{
    int tickSlot = s_ticks.best_source(frame);
    uint32_t tickFrame = (tickSlot >= 0) ? s_ticks.frames[tickSlot] : 0;

    int anchorBack = -1; uint32_t anchorFrame = 0;
    for (int b = 0; b < s_anchors.count; b++) {   // newest-first: break on hit
        const uint8_t *data = nullptr; uint32_t len = 0, tag = 0;
        if (!s_anchors.peek_at(b, &data, &len, &tag)) continue;
        if (tag <= frame && tag + 1 >= s_tape.validFrom) {
            anchorBack = b; anchorFrame = tag; break;
        }
    }

    if (tickSlot < 0 && anchorBack < 0) return false;
    if (tickSlot >= 0 && tickFrame >= anchorFrame) {
        if (!rewind3dsRestoreState(s_tickPool + (size_t)tickSlot * REWIND_SLOT_SIZE,
                s_tickLens[tickSlot])) return false;
        *sourceFrame = tickFrame;
    } else {
        const uint8_t *data = nullptr; uint32_t len = 0, tag = 0;
        s_anchors.peek_at(anchorBack, &data, &len, &tag);
        if (!rewind3dsRestoreState(data, len)) return false;
        *sourceFrame = anchorFrame;
    }
    return true;
}

// synchronous materialization: the caller has already shown "Loading..."
// when the estimate warranted it
static bool v2Materialize(uint32_t frame)
{
    s_prefetchTarget = 0;
    rewind3dsReplayEnd();
    uint32_t source = 0;
    if (!v2LoadSource(frame, &source)) return false;
    if (source < frame) {
        rewind3dsReplayBegin(source);
        rewind3dsReplayRun(frame, 0);
        rewind3dsReplayEnd();
        v2FreezeTick(frame, frame);   // revisits become free
    }
    return true;
}

int rewind3dsEstimateRestoreFrames(int back)
{
    if (!s_v2) return 0;
    if (back < 0 || back >= rewind3dsCount()) return 0;
    uint32_t f = v2TickFrame(back);
    if (s_ticks.find(f) >= 0) return 0;

    int tickSlot = s_ticks.best_source(f);
    uint32_t best = (tickSlot >= 0) ? s_ticks.frames[tickSlot] : 0;
    for (int b = 0; b < s_anchors.count; b++) {
        const uint8_t *data = nullptr; uint32_t len = 0, tag = 0;
        if (!s_anchors.peek_at(b, &data, &len, &tag)) continue;
        if (tag <= f && tag + 1 >= s_tape.validFrom) {
            if (tag > best) best = tag;
            break;
        }
    }
    if (best == 0) return -1;
    return (int)(f - best);
}

// background fill: a slice of replay per UI frame, building the missing
// tick nearest to the cursor. The game screen shows retained textures
// while browsing, so the core is free to hold replay state between calls.
void rewind3dsPrefetchStep(int cursorBack)
{
    if (!s_v2) return;
    int count = rewind3dsCount();
    if (count == 0) return;
    if (cursorBack < 0) cursorBack = 0;
    if (cursorBack >= count) cursorBack = count - 1;
    uint32_t cursorFrame = v2TickFrame(cursorBack);

    if (s_prefetchTarget == 0) {
        uint32_t start;
        if (!v2HorizonStart(&start)) return;
        uint32_t target = s_ticks.next_missing(cursorFrame, REWIND_TICK_FRAMES,
            8, start, v2TickFrame(0));
        if (target == RewindTicks::NO_FRAME) return;
        uint32_t source = 0;
        if (!v2LoadSource(target, &source)) return;
        if (source >= target) { v2FreezeTick(target, cursorFrame); return; }
        s_prefetchTarget = target;
        rewind3dsReplayBegin(source);
    }

    if (rewind3dsReplayRun(s_prefetchTarget,
            (uint64_t)(CPU_TICKS_PER_MSEC * REWIND_PREFETCH_BUDGET_MS))) {
        rewind3dsReplayEnd();
        v2FreezeTick(s_prefetchTarget, cursorFrame);
        s_prefetchTarget = 0;
    }
}

bool rewind3dsRestoreAt(int back)
{
    if (s_v2) {
        if (back < 0 || back >= rewind3dsCount()) return false;
        uint32_t f = v2TickFrame(back);
        int slot = s_ticks.find(f);
        if (slot >= 0) {
            s_prefetchTarget = 0;
            rewind3dsReplayEnd();
            return rewind3dsRestoreState(
                s_tickPool + (size_t)slot * REWIND_SLOT_SIZE, s_tickLens[slot]);
        }
        return v2Materialize(f);
    }
    const uint8_t *data = nullptr; uint32_t len = 0, tag = 0;
    if (!s_ring.valid() || !s_ring.peek_at(back, &data, &len, &tag)) return false;
    return rewind3dsRestoreState(data, len);
}

void rewind3dsRollbackTo(int back)
{
    s_prefetchTarget = 0;
    rewind3dsReplayEnd();

    // the chosen moment becomes the present: the frame counter rewinds to
    // its tag so future captures and tape frames continue from there
    if (s_v2) {
        if (back < 0 || back >= rewind3dsCount()) return;
        uint32_t f = v2TickFrame(back);
        s_nowFrame = f;
        s_tape.truncate_to(f);
        s_ticks.drop_after(f);
        for (int i = 0; i < REWIND_V2_THUMB_SLOTS; i++)
            if (s_thumb2Tags[i] > f) s_thumb2Tags[i] = 0;
        int keep = -1;
        for (int b = 0; b < s_anchors.count; b++) {
            const uint8_t *data = nullptr; uint32_t len = 0, tag = 0;
            if (s_anchors.peek_at(b, &data, &len, &tag) && tag <= f) { keep = b; break; }
        }
        if (keep < 0) s_anchors.clear();
        else if (keep > 0) s_anchors.rollback_to(keep);
        s_framesSinceAnchor = 0;
        return;
    }

    if (!s_ring.valid()) return;
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
    // v2 must NOT defer: a deferred restore only latches - the live chip
    // keeps the present's registers, replays run on that wrong state, and
    // the stale latch gets applied over the correctly materialized moment
    // at commit ("scene B's music at scene A", field report 16/08). Here
    // restores are one-per-confirm, so the real MSU restore is affordable;
    // the paused mixer already guarantees silence while browsing.
    if (s_v2) return;
    if (!s_msuDeferred && Settings.MSU1) {
        msu1_set_restore_deferred(true);
        s_msuDeferred = true;
    }
}

void rewind3dsMsuDeferEnd()
{
    if (s_v2) {
        // resync the platform bridge to whatever state the timeline left
        if (Settings.MSU1)
            msu3dsOnEvent(Msu1Event::SavestateLoaded);
        return;
    }
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
    if (s_v2) {
        s_anchors.clear();
        s_ticks.clear();
        memset(s_thumb2Tags, 0, REWIND_V2_THUMB_SLOTS * sizeof(uint32_t));
        s_framesSinceAnchor = 0;
        s_prefetchTarget = 0;
        rewind3dsReplayEnd();
    }
    if (s_msuDeferred) {
        msu1_restore_deferred_cancel();   // never apply another game's snap
        s_msuDeferred = false;
    }
}

void rewind3dsFrameTick(bool rewindHeld, bool frameHadHeadroom)
{
    if (!settings3DS.isRomLoaded) return;
    if (!settings3DS.RewindEnabled) return;   // disabled = free

    if (!s_allocTried)
        rewind3dsAllocate();
    if (!s_v2 && !s_ring.valid()) return;

    // Counting and the tape are unconditional for every executed frame -
    // a frame that runs without a tape entry would silently desynchronize
    // every replay crossing it. Only the CAPTURES below skip busy moments.
    s_nowFrame++;
    s_frameCounter++;
    if (s_tape.valid())
        s_tape.push(s_lastPad);

    if (rewindHeld && !s_wasHeld) {
        s_timelineRequested = true;
        s_timelineFromMenu = false;
    }
    s_wasHeld = rewindHeld;

    if (snd3DS.generateSilence) return;   // SRAM autosave in flight

    if (s_v2) {
        // 1s thumbnails, decoupled from any state capture (spec A.4)
        if (s_nowFrame % REWIND_TICK_FRAMES == 0) {
            int slot = (s_nowFrame / REWIND_TICK_FRAMES) % REWIND_V2_THUMB_SLOTS;
            rewind3dsCaptureThumb(s_thumb2Pool + (size_t)slot * REWIND_THUMB_BYTES);
            s_thumb2Tags[slot] = s_nowFrame;
        }

        // opportunistic anchors at adaptive spacing (spec A.2): replays
        // reconstruct everything between them from the tape
        s_framesSinceAnchor++;
        bool force = s_framesSinceAnchor >= 2 * s_anchorGapFrames;
        bool due = s_framesSinceAnchor >= s_anchorGapFrames || s_anchors.count == 0;
        if (due && (frameHadHeadroom || force)) {
            s_framesSinceAnchor = 0;
            uint32 length = 0;
            LightLock_Lock(&snd3DS.snesAccessLock);
            bool ok = S9xFreezeGameMem(s_anchors.push_ptr(), s_anchors.slotSize, &length);
            LightLock_Unlock(&snd3DS.snesAccessLock);
            if (ok) {
                s_anchors.push_commit(length, s_nowFrame);
                if (s_meter.measured()) {
                    // spacing = 2 x tolerated latency x measured speed
                    int gap = (int)(2.0f * REWIND_ANCHOR_LATENCY * s_meter.average());
                    if (gap < REWIND_ANCHOR_GAP_MIN) gap = REWIND_ANCHOR_GAP_MIN;
                    if (gap > REWIND_ANCHOR_GAP_MAX) gap = REWIND_ANCHOR_GAP_MAX;
                    s_anchorGapFrames = gap;
                }
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
