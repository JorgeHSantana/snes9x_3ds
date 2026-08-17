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

static void timelineDrawFrame(int cursor)
{
    int width = settings3DS.SecondScreenWidth;
    int count = rewind3dsCount();

    // the menu's own palette, so the modal reads as part of the same UI
    const Theme3ds &theme = Themes[static_cast<int>(settings3DS.Theme)];
    int accent = 0x529eeb;   // user call 16/08: blue highlights in every theme
    int dotColor = theme.disabledItemTextColor;

    ui3dsSetViewport(0, 0, width, 240);
    ui3dsSetTranslate(0, 0);
    ui3dsDrawRect(0, 0, width, 240, theme.menuBackColor);

    // the menu's gray top bar crowns the screen (user mock 16/08)
    ui3dsDrawRect(0, 0, width, 24, theme.menuTopBarColor);
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 0, 6, width, 20,
        0xFFFFFF, HALIGN_CENTER, "TIMELINE");

    // how far back the cursor sits - shown under the dot strip, zero
    // units hidden (e.g. "-2min 5s", "-5s 500ms", "-500ms")
    uint32_t tag = 0;
    char label[48] = "";
    if (rewind3dsPeekInfo(cursor, &tag)) {
        uint32_t ms = (uint32_t)((uint64_t)(rewind3dsNowFrame() - tag) * 1000
            / (uint32_t)rewind3dsEmulatedFps());
        uint32_t mn = ms / 60000, sec = (ms / 1000) % 60, rem = ms % 1000;
        int n = snprintf(label, sizeof(label), "-");
        if (mn > 0)  n += snprintf(label + n, sizeof(label) - n, "%umin ", (unsigned)mn);
        if (sec > 0) n += snprintf(label + n, sizeof(label) - n, "%us ", (unsigned)sec);
        if (rem > 0 || (mn == 0 && sec == 0))
            n += snprintf(label + n, sizeof(label) - n, "%ums", (unsigned)rem);
        if (label[n - 1] == ' ') label[n - 1] = '\0';
    }

    // filmstrip: focused thumb centered, neighbours at half size.
    // "older" sits to the LEFT (like a film roll running rightwards).
    int cx = width / 2;
    int thumbY = 60;   // user mock 16/08: the whole group (strip+dots+label)
                       // balances in the 24..220 band, not the strip alone
    const uint8_t *thumb = rewind3dsThumb(cursor);
    if (thumb != NULL) {
        ui3dsDrawRect(cx - REWIND_THUMB_W / 2 - 2, thumbY - 2,
                      cx + REWIND_THUMB_W / 2 + 2, thumbY + REWIND_THUMB_H + 2,
                      accent);
        timelineDrawThumb(thumb, cx - REWIND_THUMB_W / 2, thumbY, 1);
    }
    const uint8_t *older = rewind3dsThumb(cursor + 1);
    if (older != NULL) {
        timelineDrawThumb(older, cx - REWIND_THUMB_W / 2 - 58, thumbY + 30, 3);
    }
    const uint8_t *newer = rewind3dsThumb(cursor - 1);
    if (newer != NULL) {
        timelineDrawThumb(newer, cx + REWIND_THUMB_W / 2 + 8, thumbY + 30, 3);
    }

    // dot strip: newest on the right, cursor highlighted
    if (count > 0) {
        int spacing = (width - 40) / count;
        if (spacing > 10) spacing = 10;
        if (spacing < 2) spacing = 2;
        int stripWidth = spacing * (count - 1);
        int xRight = width / 2 + stripWidth / 2;
        int dotY = 175;
        for (int i = 0; i < count; i++) {
            int x = xRight - i * spacing;   // i = frames back, rightmost = newest
            if (i == cursor) {
                ui3dsDrawRect(x - 2, dotY - 3, x + 3, dotY + 4, accent);
            } else {
                ui3dsDrawRect(x, dotY - 1, x + 2, dotY + 2, dotColor);
            }
        }
    }

    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 0, 190, width, 204,
        theme.normalItemTextColor, HALIGN_CENTER, label);

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
        // black the RGB565 buffers out first: zero is black in every pixel
        // format, so the switch never flashes the old image reinterpreted
        // as garbage (field report 16/08); the wallpaper passes below
        // repaint both buffers right away - no vblank wait in between
        for (int i = 0; i < 2; i++) {
            u16 fbw = 0, fbh = 0;
            u8 *fb = gfxGetFramebuffer(settings3DS.SecondScreen, GFX_LEFT, &fbw, &fbh);
            if (fb != NULL) memset(fb, 0, (size_t)fbw * fbh * 2);
            gfxScreenSwapBuffers(settings3DS.SecondScreen, false);
        }
        gfxSetScreenFormat(settings3DS.SecondScreen, previousFormat);
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
    bool stateDirty = false;   // a restore ran; cancel must reload the present
    bool bottomRestored = false;
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
    // Heading back to the MENU (Open Timeline -> B): the menu repaints the
    // whole second screen on the very next frame and runs in the same
    // RGB565 format the timeline uses - painting the wallpaper in between
    // is the blink the field report describes (16/08). Only a return to
    // the GAME needs the wallpaper back.
    bool menuNext = !committed && fromMenu;
    if (!bottomRestored && !menuNext) {
        timelineRestoreSecondScreen(previousFormat);
    }
    menu3dsSetScreenDirty(true, true);

    rewind3dsSetTimelineActive(false);
    rewind3dsMsuDeferEnd();
    snd3dsResumeMixing();
    input3dsWaitForRelease();   // the confirming press must not reach the game
}


// Live rewind (hold, user design 17/08): the game FREEZES and dims like
// the pause screen while stored moments walk back under the button -
// one moment per capture interval at first (1x), accelerating the
// longer the hold lasts. Releasing runs the configured 3..2..1 on the
// frozen frame and play resumes from the shown moment.
// currKeysHeld only refreshes inside the emulation input scan, which the
// frozen modal never runs - releasing the trigger never stopped the walk
// (field bug 17/08). Read the pad directly instead.
static bool timelineHoldHotkeyHeld()
{
    hidScanInput();
    u32 held = hidKeysHeld();
    const auto &hk = settings3DS.UseGlobalEmuControlKeys
        ? settings3DS.GlobalButtonHotkeys[HOTKEY_REWIND_HOLD]
        : settings3DS.ButtonHotkeys[HOTKEY_REWIND_HOLD];
    return hk.IsHeld(held);
}

void rewind3dsHoldShow()
{
    if (rewind3dsCount() == 0) return;
    if (GPU3DS.profilingMode != PROFILING_OFF) return;

    snd3dsDrainMixing();
    rewind3dsMsuDeferBegin();
    rewind3dsSetTimelineActive(true);   // stepped frames read a neutral pad

    uint32_t startNow = rewind3dsNowFrame();
    int heldFrames = 0;
    int captureFrames = rewind3dsCaptureIntervalFrames();
    char badge[40];

    snprintf(badge, sizeof(badge), "\x9d");
    notif3dsTrigger(Notif::Misc, Notif::Type::Info, settings3DS.GameScreen,
        3600000.0, badge);
    timelineRenderGameScreen(true);

    while (aptMainLoop() && GPU3DS.emulatorState != EMUSTATE_END) {
        if (!timelineHoldHotkeyHeld()) break;

        heldFrames++;
        int phase = heldFrames < 120 ? 0 : heldFrames < 240 ? 1
                  : heldFrames < 360 ? 2 : 3;
        int interval = captureFrames >> phase;
        if (interval < 5) interval = 5;

        if ((heldFrames % interval) == 0) {
            if (!rewind3dsHoldStepBack())
                break;   // history exhausted: release into the countdown
            uint32_t ds = (startNow - rewind3dsNowFrame()) * 10
                / (uint32_t)rewind3dsEmulatedFps();
            if (ds >= 600)
                snprintf(badge, sizeof(badge), "\x9d -%umin %u.%us",
                    (unsigned)(ds / 600), (unsigned)((ds % 600) / 10), (unsigned)(ds % 10));
            else
                snprintf(badge, sizeof(badge), "\x9d -%u.%us",
                    (unsigned)(ds / 10), (unsigned)(ds % 10));
            notif3dsTrigger(Notif::Misc, Notif::Type::Info,
                settings3DS.GameScreen, 3600000.0, badge);
            timelineShowFrame();   // paints the restored moment, dimmed
        } else if ((heldFrames % interval) != 0) {
            gpu3dsWaitForVBlank(settings3DS.GameScreen);
        }
    }

    notif3dsHide();

    // release: the configured countdown on the frozen dimmed frame
    int framesPerStep = 0;
    switch (settings3DS.RewindCountdown) {
        case 0: framesPerStep = 0;  break;
        case 1: framesPerStep = 15; break;
        case 3: framesPerStep = 60; break;
        default: framesPerStep = 30; break;
    }
    for (int step = 3; framesPerStep > 0 && step >= 1; step--) {
        char overlay[40];
        snprintf(overlay, sizeof(overlay), "Resuming in %d...", step);
        notif3dsTrigger(Notif::Paused, Notif::Type::Default,
            settings3DS.GameScreen, 3600000.0, overlay);
        timelineRenderGameScreen(true);
        for (int f = 0; f < framesPerStep; f++)
            gpu3dsWaitForVBlank(settings3DS.GameScreen);
    }
    if (framesPerStep > 0)
        notif3dsHide();

    rewind3dsSetTimelineActive(false);
    rewind3dsMsuDeferEnd();
    snd3dsResumeMixing();
    input3dsWaitForRelease();   // the release must not leak into the game
}
