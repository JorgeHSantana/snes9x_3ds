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

static void timelineDrawFrame(int cursor, int shownBack, int countdownStep)
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

    // countdown phase replaces the filmstrip with the big number
    if (countdownStep > 0) {
        char big[8];
        snprintf(big, sizeof(big), "%d", countdownStep);
        ui3dsDrawRect(width / 2 - 40, 90, width / 2 + 40, 150, TIMELINE_PANEL_COLOR);
        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen,
            0, 110, width, 130, TIMELINE_TEXT_COLOR, HALIGN_CENTER, big);
        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 0, 200, width, 214,
            TIMELINE_DIM_TEXT_COLOR, HALIGN_CENTER, "B: cancel");
        return;
    }

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

    // status + hints
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 0, 170, width, 184,
        TIMELINE_TEXT_COLOR, HALIGN_CENTER,
        shownBack == cursor ? "Showing this moment - A: resume here"
                            : "A: show this moment");
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 0, 200, width, 214,
        TIMELINE_DIM_TEXT_COLOR, HALIGN_CENTER,
        "< / >: navigate    B: back to present");
}

static void timelinePresent()
{
    impl3dsFlushScreen(settings3DS.SecondScreen, false, false);
    gfxScreenSwapBuffers(settings3DS.SecondScreen, false);
    gpu3dsWaitForVBlank(settings3DS.SecondScreen);
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

    int cursor = 0;
    int shownBack = -1;        // -1 = game screen still shows the present
    bool committed = false;

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
            if (down & KEY_B) {
                countdownStep = 0;           // abort back to browsing
            } else if (++countdownFrames >= framesPerStep) {
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

            if (down & KEY_A) {
                if (shownBack != cursor) {
                    if (rewind3dsRestoreAt(cursor)) {
                        timelineShowFrame();
                        shownBack = cursor;
                    }
                } else if (framesPerStep == 0) {
                    committed = true; break;
                } else {
                    countdownStep = 3;
                    countdownFrames = 0;
                }
            }
        }

        timelineDrawFrame(cursor, shownBack, countdownStep);
        timelinePresent();
    }

    if (committed) {
        // the shown state is the live state; drop the abandoned future
        rewind3dsRollbackTo(cursor);
    } else if (shownBack >= 0 && havePresent) {
        // cancel: put the present back and repaint it
        if (rewind3dsRestorePresent()) {
            timelineShowFrame();
        }
    }

    // restore the second screen for the emulator's own rendering
    if (gfxGetScreenFormat(settings3DS.SecondScreen) != previousFormat) {
        gfxSetScreenFormat(settings3DS.SecondScreen, previousFormat);
        gpu3dsWaitForVBlank(settings3DS.SecondScreen);
    }
    menu3dsSetScreenDirty(true, true);

    rewind3dsSetTimelineActive(false);
    rewind3dsMsuDeferEnd();
    snd3dsResumeMixing();
    input3dsWaitForRelease();   // the confirming press must not reach the game
}
