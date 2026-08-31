#ifndef _3DSSETTINGS_H_
#define _3DSSETTINGS_H_

#include <stdio.h>
#include <array>
#include <limits.h>
#include <3ds.h>

#ifndef VERSION_MAJOR
#define VERSION_MAJOR 0
#endif

#ifndef VERSION_MINOR
#define VERSION_MINOR 0
#endif

#ifndef VERSION_MICRO
#define VERSION_MICRO 0
#endif

#define SCREEN_TOP_WIDTH        400
#define SCREEN_BOTTOM_WIDTH     320
#define SCREEN_HEIGHT           240

#define SAVESLOTS_MAX   5

#define HOTKEY_OPEN_MENU            0
#define HOTKEY_FAST_FORWARD_TOGGLE  1
#define HOTKEY_SWAP_CONTROLLERS     2
#define HOTKEY_SCREENSHOT           3
#define HOTKEY_QUICK_SAVE           4
#define HOTKEY_QUICK_LOAD           5
#define HOTKEY_SAVE_SLOT_NEXT       6
#define HOTKEY_SAVE_SLOT_PREV       7
#define HOTKEY_FAST_FORWARD_HOLD    8
#define HOTKEY_REWIND_HOLD          9
#define HOTKEYS_COUNT   10

#define OPACITY_STEPS               20
#define SCANLINE_INTENSITY_MAX      8   // 47% brightness

#define MENU_ENTRY_CONTEXT_MENU     -2
#define MENU_CONTINUE_GAME          -3

namespace Setting {
    enum class ScreenFilter {
        Sharp,      // GPU_NEAREST
        Smooth,     // GPU_LINEAR
        Balanced,   // GPU_NEAREST base + low-alpha GPU_LINEAR overlay
    };

    enum class ScreenStretch {
        None,                  // 1:1 Native (256x224, 256x240)
        Aspect_4_3,            // Stretch width only to 298
        CrtAspect,             // Stretch width only to 292 (8:7 PAR)
        Fit_4_3,               // 4:3 Fit: Stretch to 320 x 240
        Fit_8_7,               // 8:7 Fit: Stretched when 224 lines, No Stretch when 240 lines (e.g. Super Mario Kart PAL)
        Full = 6,              // Fullscreen: Stretch to GameScreenWidth x 240
    };

    enum class ThumbnailMode {
        None,
        Boxart,
        Title,
        Gameplay,
    };

    enum class AssetMode {
        None,
        Default,       // Built-in
        Adaptive,      // Custom, else Default
        CustomOnly,    // Custom or nothing
    };

    enum class Theme {
        DarkMode,
        RetroArch,
        Original,
    };

    enum class Font {
        Tempesta,
        Ronda,
        Arial,
    };

    enum class Framerate {
        UseRomRegion,
        ForceFps60,
    };

    enum class FrameSync {
        VBlank,
        Sleep,
    };

    enum class Intensity3D {
        Standard,
        Medium,
        High,
    };

    enum class EnhancedResolution {
        Off,         // native 256px render
        Standard,    // 512px internal render (keeps 3D)
        Wide,        // 512px internal render + wide 800px screen (disables 3D)
    };
}

template <int Count>
struct ButtonMapping {
    std::array<u32, Count> MappingBitmasks;

    bool operator==(const ButtonMapping& other) const {
        return this->MappingBitmasks == other.MappingBitmasks;
    }

    bool IsHeld(u32 held3dsButtons) const {
        for (u32 mapping : MappingBitmasks) {
            if (mapping != 0 && (mapping & held3dsButtons) == mapping) {
                return true;
            }
        }

        return false;
    }

    void SetSingleMapping(u32 mapping) {
        SetDoubleMapping(mapping, 0);
    }

    void SetDoubleMapping(u32 mapping0, u32 mapping1) {
        if (Count > 0) {
            MappingBitmasks[0] = mapping0;
        }
        if (Count > 1) {
            MappingBitmasks[1] = mapping1;
        }
        if (Count > 2) {
            for (size_t i = 2; i < MappingBitmasks.size(); ++i) {
                MappingBitmasks[i] = 0;
            }
        }
    }
};

typedef struct {

    // --- GENERAL ---
    Setting::Theme Theme;
    Setting::Font Font;
    Setting::ThumbnailMode GameThumbnailType;
    gfxScreen_t GameScreen;
    bool Disable3DSlider;
    Setting::Intensity3D Intensity3D;
    bool LogFileEnabled;    // Write logs to sdmc:/3ds/snes9x_3ds/debug_<APP_VERSION>_session.log
    int CurrentSaveSlot;    // remember last used save slot (1 - 5)

    // --- SELF-UPDATER (issue #64) ---
    int UpdateChannel;      // 0 = stable releases, 1 = nightly builds
    bool UpdateAutoCheck;   // check for a new build when the emulator starts

    // --- FILE MENU ---
    char defaultDir[PATH_MAX];
    char lastSelectedDir[PATH_MAX];
    char lastSelectedFilename[NAME_MAX + 1];

    // --- OSD & VIDEO ---
    Setting::AssetMode  GameOverlay;
    bool                GameOverlayAutoFit;
    int                 ScanlineIntensity;      // 0 - Off, 1..SCANLINE_INTENSITY_MAX - dark-row alpha
    Setting::AssetMode  GameScreenBg;
    int                 GameScreenBgOpacity;    // 20 - Maxium opacity
    Setting::AssetMode  SecondScreenBg;
    int                 SecondScreenBgOpacity;

    bool                ShowFPS;
    int                 Overclock;            // New 3DS clock: 0 = 268MHz (Old 3DS speed), 1 = 804MHz
    int                 RewindCountdown;      // timeline resume countdown: 0 off, 1 250ms, 2 500ms, 3 1s
    int                 RewindEnabled;        // 0 = no captures, no RAM; 1 = recording on (menu + hotkey)
    int                 RewindMaxWindow;      // history ceiling: 0 = 30s, 1 = 1min, 2 = maximum
    int                 RewindMaxWait;        // capture patience: 0..3 = 1s/2s/4s/8s

    Setting::ScreenStretch ScreenStretch;
    Setting::ScreenFilter ScreenFilter;         // User preference for SNES_MAIN in stretched modes.
                                                // No Stretch enforces sharp (nearest) at render time.
    bool                CropEnabled;            // master toggle for the per-game crop/overscan settings
    int                 CropTop;                // top crop value in scanlines
    int                 CropBottom;             // bottom crop value in scanlines
    bool                Overscan;               // zoom (cropped) ingame screen to fit height

    // --- GAME-SPECIFIC ---
    int                 MaxFrameSkips;          // 0 - disable,
                                                // 1 - enable (max 1 consecutive skipped frame)
                                                // 2 - enable (max 2 consecutive skipped frames)
                                                // 3 - enable (max 3 consecutive skipped frames)
                                                // 4 - enable (max 4 consecutive skipped frames)


    Setting::Framerate  Framerate;              // 0 - Default based on Game region
                                                // 1 - Force 60 FPS
    Setting::FrameSync  FrameSync;              // 0 - VBlank
                                                // 1 - Sleep

    int                 PaletteFix;             // Palette In-Frame Changes
                                                //   1 - Enabled - Default.
                                                //   2 - Disabled - Style 1.
                                                //   3 - Disabled - Style 2.

    u8                  PaletteDeferBgMask;     // Advanced: skip re-rendering these BG layers on mid-frame
                                                // palette changes (bit i = LAYER_BGi). 0 - render all.

    bool                Mode7BilinearFilter;    // Bilinear filter for the Mode 7 background
                                                // texture. Default false; opt-in because it
                                                // changes the characteristic Mode 7 look.

    Setting::EnhancedResolution EnhancedResolution;  // Off / Standard (512px render) / 2x Screen (512px + wide)

    int                 Volume;                 // 0: 100%, 1: 125%, 2: 150%, 3: 175%, 4: 200%
    int                 GlobalVolume;
    int                 Msu1VideoFps;       // FMV presentation cap: 0=Off, 1=40, 2=30, 3=24, 4=20 (global)
    int                 Msu1Volume;         // MSU-1 balance gauge 0..8 (25%/step, 4 = same as SNES), per-game
    int                 GlobalMsu1Volume;   // global counterpart, governed by UseGlobalVolume

    int                 AudioBuffer;            // wavebuf depth: 0=Low(4), 1=Normal(8), 2=High(16)

    bool                AutoSavestate;          // Automatically save the the current state when the emulator is closed or the game is changed
    bool                SaveStateScreenshots;   // Opt-in: save a screenshot next to each savestate

    int                 SRAMSaveInterval;       // SRAM Save Interval
                                                //   1 - 1 second.
                                                //   2 - 10 seconds
                                                //   3 - 60 seconds
                                                //   4 - Never

    bool                ForceSRAMWriteOnPause;  // If the SRAM should be written to SD even when no change was detected.
                                                // Some games (eg. Yoshi's Island) don't detect SRAM writes correctly.

    // --- CONTROLS ---
    std::array<::ButtonMapping<1>, HOTKEYS_COUNT> ButtonHotkeys;
    std::array<::ButtonMapping<1>, HOTKEYS_COUNT> GlobalButtonHotkeys;

    bool      BindCirclePad;                    // Use Circle Pad as D-Pad for gaming
    bool      GlobalBindCirclePad;

    std::array<std::array<int, 4>, 10> ButtonMapping;
    std::array<std::array<int, 4>, 10> GlobalButtonMapping;

    std::array<int, 8>   Turbo;                 // Turbo buttons: 0 - No turbo, 1 - Release/Press every alt frame.
                                                // Indexes: 0 - A, 1 - B, 2 - X, 3 - Y, 4 - L, 5 - R
    std::array<int, 8>   GlobalTurbo;

    bool      UseGlobalEmuControlKeys;          // Use global emulator control keys for all games
    bool      UseGlobalBindCirclePad;           // Use Circle Pad as D-Pad
    bool      UseGlobalButtonMappings;          // Use global button mappings for all games
    bool      UseGlobalTurbo;
    bool      UseGlobalVolume;

    // --- RUNTIME / CALCULATED ---
    // Not saved to config
    const char           *RootDir;

    gfxScreen_t         SecondScreen;
    int                 GameScreenWidth;
    int                 SecondScreenWidth;

    int                 StretchWidth;
    int                 StretchHeight;
    long                TicksPerFrame;

    bool                TurboMode;             // Effective fast-forward state (toggle and/or hold hotkeys)

    bool                LayerEnabled[8];       // Debug: per-layer enable toggle, index = LAYER_ID
                                               // (BG0-3, OBJ, Backdrop, Color Math, Brightness).
                                               // All true by default.

    int                 StereoDepth[5];        // Stereoscopic 3D depth per layer
    int                 StereoDepthP1[5];      // priority-1 depth per layer (issue #60)
    int                 StereoDepthOBJHi[2];   // sprite priority 2/3 depths (issue #60;
                                               // priorities 0/1 ride [4] of the two
                                               // arrays above, like the BGs)
    int                 StereoShiftMode;       // 0 = discrete (whole pixels, solid layers)
                                               // 1 = continuous (smooth slider, may split layers) (BG1-4, Sprites),
                                               // -8..+8: + pops out of the screen, - sinks into it.
                                               // Saved to sd:/3ds/snes9x_3ds/stereo3d/<rom>.3d
                                               // (shareable), NOT to the regular config.
    int                 StereoFade;            // 0..8: darkens layers by how deep they sink (same .3d file)
    int                 StereoHaze;            // 0..8: fogs layers by how deep they sink (same .3d file)
    int                 StereoBlur;            // 0..8: ghost-blurs layers outside the focus zone,
                                               // linearly with distance to the zone edge (same .3d file)
    int                 StereoFocusBack;       // -8..0: back edge of the focus zone (default -1)
    int                 StereoFocusFront;      // 0..+8: front edge of the focus zone (default +1)
    int                 StereoEdgeMode;        // parallax edge cleanup: 0 = Off, 1 = Trim
                                               // (game window narrows, scale kept), 2 = Zoom
                                               // (crop absorbed by the stretch; default)

    // --- PER-SCENE 3D PROFILES (issue #23) ---
    // The flat Stereo* fields above are the DEFAULT profile. Named
    // profiles bind to PPU scene signatures and override them while the
    // matching scene is on screen (30-frame hysteresis, depths lerped).
    #define STEREO_PROFILES_MAX 8
    #define STEREO_BINDS_MAX    16
    struct SStereoProfile {
        char Name[16];
        int  Depth[5];
        int  DepthP1[5];   // per-priority split (issue #60); mirrors Depth until edited
        int  DepthOBJHi[2];// sprite priorities 2/3 (0/1 ride [4] above)
        int  Fade, Haze, Blur;
        int  FocusBack, FocusFront;
        int  EdgeMode;
    };
    struct SStereoBind {
        u64  Sig;          // packed tuple: b0=2105 b1=TM b2=TS b3=2130 b4=2131 b5=2106 b6=420C
        u64  Mask;         // per-byte compare mask (00 byte = ignore that register)
        u64  Sig2;         // packed VRAM bases: b0=2101 b1..b6=2107..210C
        u64  Mask2;
        int  WatchVal;     // expected WRAM watch byte (-1 = not used)
        int  ProfileIdx;
    };
    SStereoProfile      StereoProfiles[STEREO_PROFILES_MAX];
    int                 StereoProfilesCount;
    SStereoBind         StereoBinds[STEREO_BINDS_MAX];
    int                 StereoBindsCount;
    int                 StereoWatchAddr;   // WRAM offset of the game-mode byte
                                           // (WATCH= in the .3d); -1 = none

    bool                isNew3DS;
    bool                isRomFsLoaded;
    bool                isRomLoaded;
    bool                isDirty;               // needs saving to disk
    bool                cheatsDirty;
    bool                menuTabDirty[5];    // one per TAB_* id (see 3dsmenu.h)
} S9xSettings3DS;

extern S9xSettings3DS settings3DS;

void settings3dsResetGlobalDefaults();
void settings3dsResetGameDefaults();
void settings3dsUpdate(bool includeGameSettings);

// per-scene 3D profiles (issue #23)
void settings3dsStereoApplyDefault();
void settings3dsStereoApplyProfile(int idx);      // -1 = flat Default
void settings3dsStereoMarkReapply();              // issue #61 preview handoff
void settings3dsStereoFrameTick();
// arm the capture: observe the scene for ~5s after resuming, learn the
// per-byte mask from the bits that flap, then bind the screen to the
// profile (replacing any bind that previously matched it)
void settings3dsStereoArmCapture(int profileIdx);
bool settings3dsStereoReleaseScreen();
const char *settings3dsStereoActiveName();
void settings3dsStereoMatchInfo(char *out, size_t size);   // diagnostics text
int settings3dsStereoActiveIndex();   // -1 = Default profile
void settings3dsApplyScreenLayout();
void settings3dsApplyScreenStretch();

const char *settings3dsGetAppVersion(const char *prefix, const char *suffix = NULL);

#endif
