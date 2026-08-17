#include <cstdio>
#include <cstring>

#include <3ds.h>

#include "3dsrewind.h"
#include "3dsrewindring.h"
#include "3dssettings.h"
#include "3dssound.h"
#include "3dsui.h"
#include "3dsimpl.h"
#include "3dsgpu.h"
#include "3dsinput.h"
#include "3dsmenu.h"
#include "3dsui_img.h"
#include "3dsui_notif.h"

#include "3dsthemes.h"

// ---------------------------------------------------------------------------
// Rewind timeline (issue #42): modal screen entered by tapping the Rewind
// hotkey. The game stays frozen on its screen; the second screen draws a
// filmstrip of snapshot thumbnails plus a dot strip, all software-rendered
// like the pause menu. First A materializes the focused snapshot (the game
// screen shows the real frame); second A runs the cancellable countdown and
// resumes play from there. B returns to the present at any stage.
// ---------------------------------------------------------------------------

#define TIMELINE_BG_COLOR        0x101418
#define TIMELINE_PANEL_COLOR     0x1C242C
#define TIMELINE_ACCENT_COLOR    0x529eeb
#define TIMELINE_TEXT_COLOR      0xCCCCCC
#define TIMELINE_DIM_TEXT_COLOR  0x777777
#define TIMELINE_DOT_COLOR       0x555555

#define TIMELINE_REPEAT_DELAY_FRAMES  15
#define TIMELINE_REPEAT_RATE_FRAMES   4

// second-screen framebuffer write, same convention as 3dsui.cpp
static inline void timelinePutPixel(u16 *fb, int x, int y, u16 color)
{
    fb[x * 240 + (239 - y)] = color;
}

// blit a REWIND_THUMB_W x REWIND_THUMB_H RGB565 row-major thumb, decimated
// by 'shrink' (1 = full size, 2 = half); zoom magnifies each source pixel
// (2 = double size - the focused thumb is the only preview now, so it
// earns the screen space)
static void timelineDrawThumb(const uint8_t *thumb, int x0, int y0, int shrink, int zoom = 1)
{
    u16 *fb = (u16 *)gfxGetFramebuffer(settings3DS.SecondScreen, GFX_LEFT, NULL, NULL);
    if (fb == NULL || thumb == NULL) return;

    const uint16_t *src = (const uint16_t *)thumb;
    int w = (REWIND_THUMB_W / shrink) * zoom;
    int h = (REWIND_THUMB_H / shrink) * zoom;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            timelinePutPixel(fb, x0 + x, y0 + y,
                src[(y / zoom * shrink) * REWIND_THUMB_W + (x / zoom * shrink)]);
        }
    }
}

static void timelineDrawFrame(int cursor)
{
    int width = settings3DS.SecondScreenWidth;
    int count = rewind3dsCount();

    ui3dsSetViewport(0, 0, width, 240);
    ui3dsSetTranslate(0, 0);
    ui3dsDrawRect(0, 0, width, 240, TIMELINE_BG_COLOR);

    // header: how far back the cursor sits
    uint32_t tag = 0;
    char label[48];
    if (rewind3dsPeekInfo(cursor, &tag)) {
        float secondsBack = (float)(rewind3dsNowFrame() - tag) / 60.0f;
        snprintf(label, sizeof(label), "Rewind  -%.1fs", secondsBack);
    } else {
        snprintf(label, sizeof(label), "Rewind");
    }
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 0, 8, width, 22,
        TIMELINE_ACCENT_COLOR, HALIGN_CENTER, label);

    // filmstrip: the focused thumb doubled (the deciding preview - the
    // frame only materializes AFTER Yes), neighbours at half size.
    // "older" sits to the LEFT (like a film roll running rightwards).
    int cx = width / 2;
    int thumbY = 38;
    const uint8_t *thumb = rewind3dsThumb(cursor);
    if (thumb != NULL) {
        ui3dsDrawRect(cx - REWIND_THUMB_W - 2, thumbY - 2,
                      cx + REWIND_THUMB_W + 2, thumbY + REWIND_THUMB_H * 2 + 2,
                      TIMELINE_ACCENT_COLOR);
        timelineDrawThumb(thumb, cx - REWIND_THUMB_W, thumbY, 1, 2);
    }
    const uint8_t *older = rewind3dsThumb(cursor + 1);
    if (older != NULL) {
        timelineDrawThumb(older, cx - REWIND_THUMB_W - 58, thumbY + 45, 2);
    }
    const uint8_t *newer = rewind3dsThumb(cursor - 1);
    if (newer != NULL) {
        timelineDrawThumb(newer, cx + REWIND_THUMB_W + 8, thumbY + 45, 2);
    }

    // dot strip: newest on the right, cursor highlighted
    if (count > 0) {
        int spacing = (width - 40) / count;
        if (spacing > 10) spacing = 10;
        if (spacing < 2) spacing = 2;
        int stripWidth = spacing * (count - 1);
        int xRight = width / 2 + stripWidth / 2;
        int dotY = 172;
        for (int i = 0; i < count; i++) {
            int x = xRight - i * spacing;   // i = frames back, rightmost = newest
            if (i == cursor) {
                ui3dsDrawRect(x - 2, dotY - 3, x + 3, dotY + 4, TIMELINE_ACCENT_COLOR);
            } else {
                ui3dsDrawRect(x, dotY - 1, x + 2, dotY + 2, TIMELINE_DOT_COLOR);
            }
        }
    }

    // hints in the menu's own bottom bar, same glyphs
    MenuButton buttons[] = {
        { "Resume", "\x0cc", 0x800d1d },
        { "Back",   "\x0cd", 0x999409 },
    };
    menu3dsDrawBottomBar(buttons, 2);
}

static void timelinePresent()
{
    impl3dsFlushScreen(settings3DS.SecondScreen, false, false);
    gfxScreenSwapBuffers(settings3DS.SecondScreen, false);
    gpu3dsWaitForVBlank(settings3DS.SecondScreen);
}

// recomposite the game screen from the retained textures - no emulation.
// paused=true dims it (the pause-overlay look); notifications draw on top.
static void timelineRenderGameScreen(bool dimmed)
{
    notif3dsSync();   // upload pending overlay text before the frame opens
    gpu3dsFrameBegin(C3D_FRAME_SYNCDRAW, false);
    impl3dsSceneRender(true, dimmed);
    gpu3dsFrameEnd();
}

// after the modal screen closes, the emulator's second-screen wallpaper
// must come back (same 2-pass block emulatorLoop runs on entry) - without
// this the timeline image stays frozen there and it looks like the screen
// never closed
static void timelineRestoreSecondScreen(GSPGPU_FramebufferFormat previousFormat)
{
    if (gfxGetScreenFormat(settings3DS.SecondScreen) != previousFormat) {
        gfxSetScreenFormat(settings3DS.SecondScreen, previousFormat);
        gpu3dsWaitForVBlank(settings3DS.SecondScreen);
    }
    for (int pass = 0; pass < 2; pass++) {
        gpu3dsFrameBegin(C3D_FRAME_SYNCDRAW, false, true);
            gpu3dsClearScreen(settings3DS.SecondScreen);
            img3dsDrawBackground(UI_BG_SECOND);
        gpu3dsFrameEnd();
    }
}

// run exactly one emulated frame so the freshly restored state paints the
// game screen; joypad reads return neutral while the timeline is active.
// The frame presents itself already dimmed - presenting bright and dimming
// on the next loop pass flashed the screen (bug report 2026-08-16)
static void timelineShowFrame()
{
    impl3dsRunOneFrame(false, false, true);
}

void rewind3dsTimelineShow()
{
    if (rewind3dsCount() == 0) return;
    if (GPU3DS.profilingMode != PROFILING_OFF) return;

    snd3dsDrainMixing();
    rewind3dsMsuDeferBegin();
    rewind3dsSetTimelineActive(true);

    bool havePresent = rewind3dsCapturePresent();

    // second screen goes 16-bit for the software UI (menu pattern)
    GSPGPU_FramebufferFormat previousFormat = gfxGetScreenFormat(settings3DS.SecondScreen);
    if (previousFormat != GSP_RGB565_OES) {
        gfxSetScreenFormat(settings3DS.SecondScreen, GSP_RGB565_OES);
        gpu3dsWaitForVBlank(settings3DS.SecondScreen);
    }

    bool fromMenu = rewind3dsTimelineFromMenu();
    int cursor = 0;
    bool committed = false;
    bool bottomRestored = false;
    bool stateDirty = false;   // a restore ran; cancel must reload the present
    char overlayShown[40] = "";

    u32 lastHeld = 0xffffffff; // suppresses the entry press
    int repeatFrames = 0;
    int countdownStep = 0;     // 0 = browsing, 3..1 = counting
    int countdownFrames = 0;
    int framesPerStep = 0;
    switch (settings3DS.RewindCountdown) {
        case 0: framesPerStep = 0;  break;   // off - instant go
        case 1: framesPerStep = 15; break;   // 250ms
        case 3: framesPerStep = 60; break;   // 1s
        default: framesPerStep = 30; break;  // 500ms
    }

    while (aptMainLoop() && GPU3DS.emulatorState != EMUSTATE_END) {
        hidScanInput();
        u32 held = hidKeysHeld();
        u32 down = (~lastHeld) & held;
        lastHeld = held;

        if (countdownStep > 0) {
            // point of no return: no input is read during the countdown
            if (++countdownFrames >= framesPerStep) {
                countdownFrames = 0;
                if (--countdownStep == 0) { committed = true; break; }
            }
        } else {
            bool navRepeat = false;
            if (held & (KEY_DLEFT | KEY_DRIGHT | KEY_LEFT | KEY_RIGHT)) {
                repeatFrames++;
                navRepeat = repeatFrames > TIMELINE_REPEAT_DELAY_FRAMES
                    && (repeatFrames % TIMELINE_REPEAT_RATE_FRAMES) == 0;
            } else {
                repeatFrames = 0;
            }

            // left = older, right = newer
            if ((down & (KEY_DLEFT | KEY_LEFT)) || (navRepeat && (held & (KEY_DLEFT | KEY_LEFT)))) {
                if (cursor < rewind3dsCount() - 1) cursor++;
            }
            if ((down & (KEY_DRIGHT | KEY_RIGHT)) || (navRepeat && (held & (KEY_DRIGHT | KEY_RIGHT)))) {
                if (cursor > 0) cursor--;
            }

            if (down & KEY_B) break;

            // A asks first (the doubled thumb is the preview); the state
            // only loads AFTER Yes - browsing never touches the game
            // (field request 16/08: "Loading" belongs after the choice)
            if (down & KEY_A) {
                // the menu's own modal Yes/No dialog (3dsmain.cpp), with
                // the timeline as its dimmed backdrop (issue #43)
                menu3dsSetDialogBackdrop([cursor]() {
                    timelineDrawFrame(cursor);
                });
                bool confirmed = rewind3dsConfirmResume();
                menu3dsClearDialogBackdrop();
                lastHeld = 0xffffffff;   // swallow the dialog's last press
                if (confirmed) {
                    // long jumps replay whole segments (v2): announce it
                    if (rewind3dsEstimateRestoreFrames(cursor) > 60) {
                        notif3dsTrigger(Notif::Paused, Notif::Type::Default,
                            settings3DS.GameScreen, 3600000.0, "Loading...");
                        overlayShown[0] = '\0';
                        timelineRenderGameScreen(true);
                    }
                    stateDirty = true;
                    if (rewind3dsRestoreAt(cursor)) {
                        timelineShowFrame();
                        // the bottom screen returns to the wallpaper now;
                        // the countdown lives on the game screen only
                        timelineRestoreSecondScreen(previousFormat);
                        bottomRestored = true;
                        if (framesPerStep == 0) { committed = true; break; }
                        countdownStep = 3;
                        countdownFrames = 0;
                    }
                    // restore failure: stay browsing; cancel reloads F0
                }
            }
        }

        // top screen: the pause-overlay look (dimmed recomposite) with a
        // centered persistent message in the pause style, re-triggered
        // only when the wording changes
        char overlay[40];
        if (countdownStep > 0) {
            snprintf(overlay, sizeof(overlay), "Resuming in %d...", countdownStep);
        } else {
            snprintf(overlay, sizeof(overlay), "Select a moment below");
        }
        if (strcmp(overlay, overlayShown) != 0) {
            snprintf(overlayShown, sizeof(overlayShown), "%s", overlay);
            notif3dsTrigger(Notif::Paused, Notif::Type::Default, settings3DS.GameScreen,
                3600000.0, overlay);
        }
        timelineRenderGameScreen(true);

        if (countdownStep > 0) {
            gpu3dsWaitForVBlank(settings3DS.GameScreen);
        } else {
            // v2: a bounded replay slice fills the tick nearest the cursor
            // while the user reads the screen (no-op on the ring provider)
            rewind3dsPrefetchStep(cursor);
            timelineDrawFrame(cursor);
            timelinePresent();
        }
    }

    if (committed) {
        // the shown state is the live state; drop the abandoned future
        rewind3dsRollbackTo(cursor);
    } else {
        // cancel (F0): reload the state saved on entry, always
        if (havePresent && rewind3dsRestorePresent()) {
            if (stateDirty) timelineShowFrame();
        }
        // menu entry: B returns to the menu, not the game
        if (fromMenu) {
            GPU3DS.emulatorState = EMUSTATE_PAUSEMENU;
        }
    }

    notif3dsHide();
    if (!bottomRestored) {
        timelineRestoreSecondScreen(previousFormat);
    }
    menu3dsSetScreenDirty(true, true);

    rewind3dsSetTimelineActive(false);
    rewind3dsMsuDeferEnd();
    snd3dsResumeMixing();
    input3dsWaitForRelease();   // the confirming press must not reach the game
}
