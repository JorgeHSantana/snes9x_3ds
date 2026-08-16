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

static void timelineDrawBottomBar(const char *aLabel, const char *bLabel);

// second-screen framebuffer write, same convention as 3dsui.cpp
static inline void timelinePutPixel(u16 *fb, int x, int y, u16 color)
{
    fb[x * 240 + (239 - y)] = color;
}

// blit a REWIND_THUMB_W x REWIND_THUMB_H RGB565 row-major thumb, decimated
// by 'shrink' (1 = full size, 2 = half)
static void timelineDrawThumb(const uint8_t *thumb, int x0, int y0, int shrink)
{
    u16 *fb = (u16 *)gfxGetFramebuffer(settings3DS.SecondScreen, GFX_LEFT, NULL, NULL);
    if (fb == NULL || thumb == NULL) return;

    const uint16_t *src = (const uint16_t *)thumb;
    int w = REWIND_THUMB_W / shrink;
    int h = REWIND_THUMB_H / shrink;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            timelinePutPixel(fb, x0 + x, y0 + y,
                src[(y * shrink) * REWIND_THUMB_W + (x * shrink)]);
        }
    }
}

static void timelineDrawFrame(int cursor, int shownBack)
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

    // filmstrip: focused thumb centered, neighbours at half size.
    // "older" sits to the LEFT (like a film roll running rightwards).
    int cx = width / 2;
    int thumbY = 60;
    const uint8_t *thumb = rewind3dsThumb(cursor);
    if (thumb != NULL) {
        ui3dsDrawRect(cx - REWIND_THUMB_W / 2 - 2, thumbY - 2,
                      cx + REWIND_THUMB_W / 2 + 2, thumbY + REWIND_THUMB_H + 2,
                      shownBack == cursor ? TIMELINE_ACCENT_COLOR : TIMELINE_DOT_COLOR);
        timelineDrawThumb(thumb, cx - REWIND_THUMB_W / 2, thumbY, 1);
    }
    const uint8_t *older = rewind3dsThumb(cursor + 1);
    if (older != NULL) {
        timelineDrawThumb(older, cx - REWIND_THUMB_W / 2 - 62, thumbY + 15, 2);
    }
    const uint8_t *newer = rewind3dsThumb(cursor - 1);
    if (newer != NULL) {
        timelineDrawThumb(newer, cx + REWIND_THUMB_W / 2 + 12, thumbY + 15, 2);
    }

    // dot strip: newest on the right, cursor highlighted
    if (count > 0) {
        int spacing = (width - 40) / count;
        if (spacing > 10) spacing = 10;
        if (spacing < 2) spacing = 2;
        int stripWidth = spacing * (count - 1);
        int xRight = width / 2 + stripWidth / 2;
        int dotY = 150;
        for (int i = 0; i < count; i++) {
            int x = xRight - i * spacing;   // i = frames back, rightmost = newest
            if (i == cursor) {
                ui3dsDrawRect(x - 2, dotY - 3, x + 3, dotY + 4, TIMELINE_ACCENT_COLOR);
            } else {
                ui3dsDrawRect(x, dotY - 1, x + 2, dotY + 2, TIMELINE_DOT_COLOR);
            }
        }
    }

    // hints live in the menu-style bottom bar, contextual on A
    timelineDrawBottomBar(shownBack == cursor ? "Resume" : "Show", "Back");
}

// bottom bar in the main menu's style: colored A/B chips + labels
static void timelineDrawBottomBar(const char *aLabel, const char *bLabel)
{
    const Theme3ds &theme = Themes[static_cast<int>(settings3DS.Theme)];
    int width = settings3DS.SecondScreenWidth;

    ui3dsDrawRect(0, 240 - 16, width, 240, theme.menuBottomBarColor);
    ui3dsDrawRect(8, 240 - 14, 20, 240 - 2, 0xC93B33);
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 8, 240 - 13, 20, 240 - 2,
        0xFFFFFF, HALIGN_CENTER, "A");
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 24, 240 - 13, 120, 240 - 2,
        theme.menuBottomBarTextColor, HALIGN_LEFT, aLabel);
    ui3dsDrawRect(126, 240 - 14, 138, 240 - 2, 0xE8A220);
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 126, 240 - 13, 138, 240 - 2,
        0xFFFFFF, HALIGN_CENTER, "B");
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 142, 240 - 13, 240, 240 - 2,
        theme.menuBottomBarTextColor, HALIGN_LEFT, bLabel);
}

// Yes/No confirmation drawn in the emulator's own dialog design (same
// font, theme colors, accent bar and bottom-bar button hints), replicated
// with the ui3ds primitives - the real menu dialog machinery cannot run
// outside the menu context.
static void timelineDrawDialog(int selection)
{
    const Theme3ds &theme = Themes[static_cast<int>(settings3DS.Theme)];
    int width = settings3DS.SecondScreenWidth;

    ui3dsSetViewport(0, 0, width, 240);
    ui3dsSetTranslate(0, 0);
    ui3dsDrawRect(0, 0, width, 240, theme.menuBackColor);

    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 20, 60, width - 20, 74,
        theme.normalItemTextColor, HALIGN_LEFT, "Rewind");
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 20, 84, width - 20, 98,
        theme.normalItemDescriptionTextColor, HALIGN_LEFT,
        "Resume the game from this moment?");

    ui3dsDrawRect(0, 110, width, 114, theme.dialogColorInfo);

    const char *options[2] = { "Yes", "No" };
    for (int i = 0; i < 2; i++) {
        int y0 = 126 + i * 22;
        if (i == selection) {
            ui3dsDrawRect(0, y0 - 4, width, y0 + 14, theme.selectedItemBackColor);
        }
        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 20, y0, width - 20, y0 + 14,
            i == selection ? theme.selectedItemTextColor : theme.normalItemTextColor,
            HALIGN_LEFT, options[i]);
    }

    timelineDrawBottomBar("Select", "Back");
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
// game screen; joypad reads return neutral while the timeline is active
static void timelineShowFrame()
{
    impl3dsRunOneFrame(false, false);
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
    int shownBack = -1;        // -1 = game screen still shows the present
    bool committed = false;
    bool dialogOpen = false;
    bool bottomRestored = false;
    int  dialogSel = 1;        // matches the menu's confirm default: "No"
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
        } else if (dialogOpen) {
            if (down & (KEY_DUP | KEY_UP | KEY_DDOWN | KEY_DOWN)) {
                dialogSel = 1 - dialogSel;
            }
            if (down & KEY_B) {
                dialogOpen = false;          // "No": back to the timeline
            }
            if (down & KEY_A) {
                dialogOpen = false;
                if (dialogSel == 0) {        // "Yes"
                    // the bottom screen returns to the wallpaper now; the
                    // countdown lives on the game screen only
                    timelineRestoreSecondScreen(previousFormat);
                    bottomRestored = true;
                    if (framesPerStep == 0) { committed = true; break; }
                    countdownStep = 3;
                    countdownFrames = 0;
                }
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

            if (down & KEY_A) {
                if (shownBack != cursor) {
                    if (rewind3dsRestoreAt(cursor)) {
                        timelineShowFrame();
                        shownBack = cursor;
                    }
                } else {
                    dialogOpen = true;
                    dialogSel = 1;           // default "No", like the menu
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
            if (dialogOpen) {
                timelineDrawDialog(dialogSel);
            } else {
                timelineDrawFrame(cursor, shownBack);
            }
            timelinePresent();
        }
    }

    if (committed) {
        // the shown state is the live state; drop the abandoned future
        rewind3dsRollbackTo(cursor);
    } else {
        // cancel (F0): reload the state saved on entry, always
        if (havePresent && rewind3dsRestorePresent()) {
            if (shownBack >= 0) timelineShowFrame();
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
