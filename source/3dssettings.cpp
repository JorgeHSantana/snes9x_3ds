
#include <cstring>

#include "snes9x.h"
#include "memmap.h"
#include "3dssettings.h"
#include "3dslog.h"
#include "3dsui_notif.h"
#include "3dsstereosig.h"
#include "3dsgpu.h"
#include "3dsmsu.h"
#include "3dssound.h"
#include "3dslcd.h"
#include "3dsui.h"

S9xSettings3DS settings3DS;

void settings3dsResetGlobalDefaults() {
    settings3DS.RootDir = "sdmc:/3ds/snes9x_3ds";
    
    memset(settings3DS.defaultDir, 0, sizeof(settings3DS.defaultDir));
    memset(settings3DS.lastSelectedDir, 0, sizeof(settings3DS.lastSelectedDir));
    memset(settings3DS.lastSelectedFilename, 0, sizeof(settings3DS.lastSelectedFilename));
    
    settings3DS.Theme = Setting::Theme::DarkMode;
    settings3DS.Font  = Setting::Font::Tempesta;
    settings3DS.GameThumbnailType = Setting::ThumbnailMode::None;
    settings3DS.SaveStateScreenshots = false;
    settings3DS.GameScreen = GFX_TOP;
    
    settings3DS.Disable3DSlider = false;
    settings3DS.Intensity3D = Setting::Intensity3D::Standard;
    settings3DS.LogFileEnabled = false;
    settings3DS.Msu1VideoFps = 0;

    settings3DS.ScreenStretch = Setting::ScreenStretch::Aspect_4_3;
    settings3DS.ScreenFilter = Setting::ScreenFilter::Smooth;
    settings3dsApplyScreenStretch();
    
    settings3DS.TicksPerFrame = TICKS_PER_FRAME_SNES_NTSC;
    settings3DS.GlobalVolume = 6;     // gauge scale: 6 = 150% (matches the old default)
    settings3DS.GlobalMsu1Volume = 4; // 4 = no extra MSU-1 boost/cut

    settings3DS.GameOverlay = Setting::AssetMode::None;
    settings3DS.GameOverlayAutoFit = false;
    settings3DS.ScanlineIntensity = 0;
    settings3DS.GameScreenBg = Setting::AssetMode::Adaptive;
    settings3DS.GameScreenBgOpacity = OPACITY_STEPS / 2;
    settings3DS.SecondScreenBg = Setting::AssetMode::Adaptive;
    settings3DS.SecondScreenBgOpacity = OPACITY_STEPS / 2;

    settings3DS.ShowFPS = false;
    settings3DS.Overclock = 1;   // New 3DS full speed by default (matches prior behavior)
    settings3DS.RewindCountdown = 2;   // 500ms steps
    settings3DS.RewindEnabled = 1;     // recording on out of the box (menu + hotkey)

    settings3DS.UseGlobalEmuControlKeys = true;
    settings3DS.UseGlobalBindCirclePad = true;
    settings3DS.UseGlobalButtonMappings = true;
    settings3DS.UseGlobalTurbo = false;
    settings3DS.UseGlobalVolume = false;

    u32 defaultButtonMapping[] = { 
      SNES_A_MASK, SNES_B_MASK, SNES_X_MASK, SNES_Y_MASK, SNES_TL_MASK, SNES_TR_MASK, 0, 0, SNES_SELECT_MASK, SNES_START_MASK 
    };

    for (int i = 0; i < 10; i++)
      settings3DS.GlobalButtonMapping[i][0] = defaultButtonMapping[i];

    settings3DS.GlobalBindCirclePad = true;

    for (int i = 0; i < HOTKEYS_COUNT; ++i)
      settings3DS.ButtonHotkeys[i].SetSingleMapping(0);

    for (int i = 0; i < 8; i++)
      settings3DS.GlobalTurbo[i] = 0;

    settings3DS.isDirty = true;
}

void settings3dsResetGameDefaults() {
    settings3DS.Framerate = Setting::Framerate::UseRomRegion;
    settings3DS.FrameSync = Setting::FrameSync::VBlank;
    settings3DS.PaletteFix = 0;
    settings3DS.PaletteDeferBgMask = 0;
    settings3DS.Mode7BilinearFilter = false;

    memset(settings3DS.LayerEnabled, true, sizeof(settings3DS.LayerEnabled));

    settings3DS.EnhancedResolution = Setting::EnhancedResolution::Off;
    settings3DS.CropEnabled = false;
    settings3DS.CropTop = 0;
    settings3DS.CropBottom = 0;
    settings3DS.Overscan = false;
    settings3DS.Volume = settings3DS.GlobalVolume;
    settings3DS.Msu1Volume = settings3DS.GlobalMsu1Volume;
    settings3DS.MaxFrameSkips = 1;
    settings3DS.CurrentSaveSlot = 1;
    settings3DS.AutoSavestate = false;
    settings3DS.SRAMSaveInterval = 4;   // Disabled
    settings3DS.ForceSRAMWriteOnPause = false;
    settings3DS.AudioBuffer = 1;

    // reset controls to global defaults (settings.cfg)
    //
    settings3DS.BindCirclePad = settings3DS.GlobalBindCirclePad;

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 4; j++) {
            settings3DS.ButtonMapping[i][j] = settings3DS.GlobalButtonMapping[i][j];
        }
    }
    
    for (int i = 0; i < 8; i++) {
        settings3DS.Turbo[i] = settings3DS.GlobalTurbo[i];
    }

    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        settings3DS.ButtonHotkeys[i] = settings3DS.GlobalButtonHotkeys[i];
    }
}

void settings3dsApplyScreenStretch() {
    settings3DS.StretchWidth = 256;
    settings3DS.StretchHeight = -1;

    switch (settings3DS.ScreenStretch)
    {
        case Setting::ScreenStretch::None:
            break;

        case Setting::ScreenStretch::Aspect_4_3:
            settings3DS.StretchWidth = 298;
            break;

        case Setting::ScreenStretch::CrtAspect:
            settings3DS.StretchWidth = 292;
            break;

        case Setting::ScreenStretch::Fit_4_3:
            settings3DS.StretchWidth = 320;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;

        case Setting::ScreenStretch::Full:
            settings3DS.StretchWidth = settings3DS.GameScreen == GFX_TOP ? SCREEN_TOP_WIDTH : SCREEN_BOTTOM_WIDTH;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;

        case Setting::ScreenStretch::Fit_8_7:
            settings3DS.StretchWidth = 274;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;
    }
}


// Per-game default for the In-Frame Palette Changes.
// 1 = Enabled, 2 = Disabled Style 1, 3 = Disabled Style 2
static int settings3dsGetGameDefaultPaletteFix()
{
    const char *name = Memory.ROMName;

    if (settings3DS.isNew3DS) 
        return 1;

    if (strcmp(name, "Bahamut Lagoon") == 0 ||
        strcmp(name, "Bahamut Lagoon Eng v3") == 0 ||
        strcmp(name, "GUN HAZARD") == 0)
        return 2;   // dialog / flashing sky palette colours

    if (strncmp(name, "JUDGE DREDD THE MOVIE", 11) == 0 ||
        strcmp(name, "Secret of MANA") == 0 ||
        strcmp(name, "SeikenDensetsu 2") == 0 ||
        strcmp(name, "WILD GUNS") == 0 ||
        strcmp(name, "BATMAN FOREVER") == 0 ||
        strcmp(name, "KIRBY SUPER DELUXE") == 0)
        return 1;

    return 3;
}

void settings3dsUpdate(bool includeGameSettings)
{
    settings3dsApplyScreenStretch();

    if (includeGameSettings)
    {
        // Update frame rate
        //
        if (Settings.PAL) {
            settings3DS.TicksPerFrame = settings3DS.Framerate == Setting::Framerate::ForceFps60 ? TICKS_PER_FRAME_SNES_NTSC : TICKS_PER_FRAME_SNES_PAL;
        } else {
            settings3DS.TicksPerFrame = TICKS_PER_FRAME_SNES_NTSC;
        }
        
        snd3dsApplyOutputVolume();
        msu3dsSetGlobalVolume(0.25f * (float)(settings3DS.UseGlobalVolume ? settings3DS.GlobalMsu1Volume : settings3DS.Msu1Volume));

        // stereo 3D config -> GPU (default profile; the per-scene matcher
        // in settings3dsStereoFrameTick may override it while running)
        settings3dsStereoApplyDefault();

        if (settings3DS.PaletteFix == 0)
            settings3DS.PaletteFix = settings3dsGetGameDefaultPaletteFix();

        if (settings3DS.PaletteFix == 1)
            SNESGameFixes.PaletteCommitLine = -2;
        else if (settings3DS.PaletteFix == 2)
            SNESGameFixes.PaletteCommitLine = 1;
        else // 3
            SNESGameFixes.PaletteCommitLine = -1;

        if (settings3DS.SRAMSaveInterval == 1)
            Settings.AutoSaveDelay = 60;
        else if (settings3DS.SRAMSaveInterval == 2)
            Settings.AutoSaveDelay = 600;
        else if (settings3DS.SRAMSaveInterval == 3)
            Settings.AutoSaveDelay = 3600;
        else
            Settings.AutoSaveDelay = -1;

        if (settings3DS.UseGlobalButtonMappings) {
            for (int i = 0; i < 10; i++)
                for (int j = 0; j < 4; j++)
                    settings3DS.ButtonMapping[i][j] = settings3DS.GlobalButtonMapping[i][j];
            
            settings3DS.BindCirclePad = settings3DS.GlobalBindCirclePad;
        }

        if (settings3DS.UseGlobalTurbo) {
            for (int i = 0; i < 8; i++) 
                settings3DS.Turbo[i] = settings3DS.GlobalTurbo[i];
        }

        if (settings3DS.UseGlobalEmuControlKeys) {
             for (int i = 0; i < HOTKEYS_COUNT; ++i) 
                settings3DS.ButtonHotkeys[i] = settings3DS.GlobalButtonHotkeys[i];
        }
        
        // Fixes the Auto-Save timer bug that causes
        // the SRAM to be saved once when the settings were
        // changed to Disabled.
        //
        if (Settings.AutoSaveDelay == -1)
            CPU.AutoSaveTimer = -1;
        else
            CPU.AutoSaveTimer = 0;
    }
}

//----------------------------------------------------------------------
// Per-scene 3D profiles (issue #23): PPU-signature matcher.
//----------------------------------------------------------------------

// computes every derived GPU3DS stereo field from a (possibly lerped)
// set of float depths + effect/zone values
static void settings3dsStereoApplyValues(const float depths[5],
    float fade, float haze, float blur, float focusB, float focusF, int edgeMode)
{
    float focusBack = focusB;
    float focusFront = focusF;
    float maxExcess = 0.0f, maxBackExcess = 0.0f, maxPop = 0.0f, maxAbs = 0.0f;

    for (int i = 0; i < 8; i++) {
        float depth = (i < 5) ? depths[i] : 0.0f;
        GPU3DS.stereoLayerDepth[i] = depth;

        if (depth > maxPop) maxPop = depth;
        if (depth > maxAbs) maxAbs = depth;
        if (-depth > maxAbs) maxAbs = -depth;

        // distance beyond the focus zone (0 inside it)
        float excess = 0.0f;
        if (depth < focusBack)
            excess = focusBack - depth;
        else if (depth > focusFront)
            excess = depth - focusFront;
        if (excess > maxExcess) maxExcess = excess;
        if (depth < focusBack && excess > maxBackExcess) maxBackExcess = excess;
    }

    GPU3DS.stereoFocusBack = focusBack;
    GPU3DS.stereoFocusFront = focusFront;
    GPU3DS.stereoMaxExcess = maxExcess;
    GPU3DS.stereoMaxBackExcess = maxBackExcess;
    GPU3DS.stereoMaxPop = maxPop;
    GPU3DS.stereoMaxAbs = maxAbs;
    GPU3DS.stereoFade = fade;
    GPU3DS.stereoHaze = haze;
    GPU3DS.stereoBlur = blur;
    GPU3DS.stereoEdgeMode = edgeMode;
}

// view of the flat default fields as a profile
static void settings3dsStereoDefaultProfile(S9xSettings3DS::SStereoProfile *p)
{
    snprintf(p->Name, sizeof(p->Name), "Default");
    for (int i = 0; i < 5; i++) p->Depth[i] = settings3DS.StereoDepth[i];
    p->Fade = settings3DS.StereoFade;
    p->Haze = settings3DS.StereoHaze;
    p->Blur = settings3DS.StereoBlur;
    p->FocusBack = settings3DS.StereoFocusBack;
    p->FocusFront = settings3DS.StereoFocusFront;
    p->EdgeMode = settings3DS.StereoEdgeMode;
}

static int s_stereoActiveIdx = -1;   // -1 = default profile

// capture state (settings3dsStereoArmCapture)
static int s_capFrames = 0;
static int s_capProfile = -1;
static u64 s_capOr, s_capAnd, s_capOr2, s_capAnd2;
static int s_capWatch;

void settings3dsStereoArmCapture(int profileIdx)
{
    s_capProfile = profileIdx;
    s_capFrames = 300;   // ~5s at 60fps
    s_capOr = s_capOr2 = 0;
    s_capAnd = s_capAnd2 = ~0ULL;
    s_capWatch = -1;
}

// unbind whatever the CURRENT scene matches (inverse of capture)
bool settings3dsStereoReleaseScreen()
{
    u8 *rr = Memory.FillRAM;
    u64 sig = (u64)rr[0x2105]
        | ((u64)rr[0x212C] << 8)  | ((u64)rr[0x212D] << 16)
        | ((u64)rr[0x2130] << 24) | ((u64)rr[0x2131] << 32)
        | ((u64)rr[0x2106] << 40) | ((u64)rr[0x420C] << 48);
    u64 sig2 = (u64)rr[0x2101]
        | ((u64)rr[0x2107] << 8)  | ((u64)rr[0x2108] << 16)
        | ((u64)rr[0x2109] << 24) | ((u64)rr[0x210A] << 32)
        | ((u64)rr[0x210B] << 40) | ((u64)rr[0x210C] << 48);
    int watch = settings3DS.StereoWatchAddr >= 0
        ? Memory.RAM[settings3DS.StereoWatchAddr & 0x1FFFF] : -1;

    bool removed = false;
    for (int i = settings3DS.StereoBindsCount - 1; i >= 0; i--) {
        const S9xSettings3DS::SStereoBind *b = &settings3DS.StereoBinds[i];
        bool hits = stereoSigBindMatches(sig, sig2, watch,
            b->Sig, b->Mask, b->Sig2, b->Mask2, b->WatchVal);
        if (hits) {
            for (int j = i; j < settings3DS.StereoBindsCount - 1; j++)
                settings3DS.StereoBinds[j] = settings3DS.StereoBinds[j + 1];
            settings3DS.StereoBindsCount--;
            removed = true;
        }
    }
    if (removed) {
        s_stereoActiveIdx = -1;
        settings3dsStereoApplyDefault();
        settings3DS.isDirty = true;
    }
    return removed;
}

int settings3dsStereoActiveIndex()
{
    if (s_stereoActiveIdx >= 0 && s_stereoActiveIdx < settings3DS.StereoProfilesCount)
        return s_stereoActiveIdx;
    return -1;
}

// Read-only diagnostics for the Tools menu: what the matcher sees on the
// screen you paused on, and which profile it picked (issue #33).
void settings3dsStereoMatchInfo(char *out, size_t size)
{
    u8 *rr = Memory.FillRAM;
    char watchLine[64];
    if (settings3DS.StereoWatchAddr >= 0) {
        snprintf(watchLine, sizeof(watchLine), "WATCH $7E%04X = %02X",
            settings3DS.StereoWatchAddr & 0xFFFF,
            Memory.RAM[settings3DS.StereoWatchAddr & 0x1FFFF]);
    } else {
        snprintf(watchLine, sizeof(watchLine), "WATCH: not set (WATCH= line in the .3d)");
    }

    snprintf(out, size,
        "Matched profile: %s\n"
        "PPU  2105=%02X TM=%02X TS=%02X 2130=%02X\n"
        "     2131=%02X 2106=%02X 420C=%02X\n"
        "VRAM 2101=%02X 2107=%02X 2108=%02X 2109=%02X\n"
        "     210A=%02X 210B=%02X 210C=%02X\n"
        "%s\n"
        "Profiles: %d   Screen binds: %d",
        settings3dsStereoActiveName(),
        rr[0x2105], rr[0x212C], rr[0x212D], rr[0x2130],
        rr[0x2131], rr[0x2106], rr[0x420C],
        rr[0x2101], rr[0x2107], rr[0x2108], rr[0x2109],
        rr[0x210A], rr[0x210B], rr[0x210C],
        watchLine,
        settings3DS.StereoProfilesCount, settings3DS.StereoBindsCount);
}

const char *settings3dsStereoActiveName()
{
    if (s_stereoActiveIdx >= 0 && s_stereoActiveIdx < settings3DS.StereoProfilesCount)
        return settings3DS.StereoProfiles[s_stereoActiveIdx].Name;
    return "Default";
}



void settings3dsStereoApplyDefault()
{
    S9xSettings3DS::SStereoProfile def;
    settings3dsStereoDefaultProfile(&def);
    const S9xSettings3DS::SStereoProfile *p =
        (s_stereoActiveIdx >= 0 && s_stereoActiveIdx < settings3DS.StereoProfilesCount)
            ? &settings3DS.StereoProfiles[s_stereoActiveIdx] : &def;

    float depths[5];
    for (int i = 0; i < 5; i++) depths[i] = (float)p->Depth[i];
    settings3dsStereoApplyValues(depths, (float)p->Fade, (float)p->Haze,
        (float)p->Blur, (float)p->FocusBack, (float)p->FocusFront, p->EdgeMode);
}

// called once per emulated frame while in-game
void settings3dsStereoFrameTick()
{
    const int HYSTERESIS_FRAMES = 30;
    const int LERP_FRAMES = 20;

    static u64 s_lastSig = ~0ULL;
    static u64 s_lastSig2 = ~0ULL;
    static int s_pendingIdx = -2;
    static int s_stableFrames = 0;
    static int s_lerpLeft = 0;
    static float s_fromDepths[5];
    static float s_fromFx[5];   // fade, haze, blur, focusBack, focusFront

    u8 *rr = Memory.FillRAM;
    u64 sig = (u64)rr[0x2105]
        | ((u64)rr[0x212C] << 8)  | ((u64)rr[0x212D] << 16)
        | ((u64)rr[0x2130] << 24) | ((u64)rr[0x2131] << 32)
        | ((u64)rr[0x2106] << 40) | ((u64)rr[0x420C] << 48);
    // VRAM base registers: scene-static and near-unique per scene, the
    // discriminator when the mode/layer tuple collides (e.g. MMX3 title
    // vs stage start)
    u64 sig2 = (u64)rr[0x2101]
        | ((u64)rr[0x2107] << 8)  | ((u64)rr[0x2108] << 16)
        | ((u64)rr[0x2109] << 24) | ((u64)rr[0x210A] << 32)
        | ((u64)rr[0x210B] << 40) | ((u64)rr[0x210C] << 48);

    if ((sig ^ s_lastSig) | (sig2 ^ s_lastSig2)) {
        log3dsWrite("[sig] 2105=%02X TM=%02X TS=%02X 2130=%02X 2131=%02X 2106=%02X 420C=%02X | 2101=%02X 2107=%02X 2108=%02X 2109=%02X 210A=%02X 210B=%02X 210C=%02X",
            rr[0x2105], rr[0x212C], rr[0x212D], rr[0x2130], rr[0x2131], rr[0x2106], rr[0x420C],
            rr[0x2101], rr[0x2107], rr[0x2108], rr[0x2109], rr[0x210A], rr[0x210B], rr[0x210C]);
        s_lastSig = sig;
        s_lastSig2 = sig2;
    }

    // capture in progress: accumulate what stays stable on this scene
    if (s_capFrames > 0) {
        s_capOr |= sig;   s_capAnd &= sig;
        s_capOr2 |= sig2; s_capAnd2 &= sig2;
        if (settings3DS.StereoWatchAddr >= 0)
            s_capWatch = Memory.RAM[settings3DS.StereoWatchAddr & 0x1FFFF];
        if (s_capFrames % 60 == 0) {
            char msg[48];
            snprintf(msg, sizeof(msg), "Capturing screen: %ds...", s_capFrames / 60);
            notif3dsTrigger(Notif::Misc, Notif::Info, settings3DS.GameScreen, 1400.0, msg);
        }
        if (--s_capFrames == 0 && s_capProfile >= 0 &&
            s_capProfile < settings3DS.StereoProfilesCount) {
            u64 mask = stereoSigCapMask(s_capOr, s_capAnd);
            u64 mask2 = stereoSigCapMask(s_capOr2, s_capAnd2);
            // drop any bind this scene currently matches (re-bind semantics)
            for (int i = settings3DS.StereoBindsCount - 1; i >= 0; i--) {
                const S9xSettings3DS::SStereoBind *b = &settings3DS.StereoBinds[i];
                bool hits = stereoSigBindMatches(s_capAnd, s_capAnd2, s_capWatch,
                    b->Sig, b->Mask, b->Sig2, b->Mask2, b->WatchVal);
                if (hits) {
                    for (int j = i; j < settings3DS.StereoBindsCount - 1; j++)
                        settings3DS.StereoBinds[j] = settings3DS.StereoBinds[j + 1];
                    settings3DS.StereoBindsCount--;
                }
            }
            if (settings3DS.StereoBindsCount < STEREO_BINDS_MAX) {
                S9xSettings3DS::SStereoBind *b =
                    &settings3DS.StereoBinds[settings3DS.StereoBindsCount++];
                b->Sig = s_capAnd & mask;   b->Mask = mask;
                b->Sig2 = s_capAnd2 & mask2; b->Mask2 = mask2;
                b->WatchVal = s_capWatch;
                b->ProfileIdx = s_capProfile;
                settings3DS.isDirty = true;
                char msg[48];
                snprintf(msg, sizeof(msg), "Screen captured: %s",
                    settings3DS.StereoProfiles[s_capProfile].Name);
                notif3dsTrigger(Notif::Misc, Notif::Success, settings3DS.GameScreen, 2500.0, msg);
                settings3DS.menuTabDirty[1] = true;   // TAB_SETTINGS: refresh 'This screen matches'
                log3dsWrite("[sig] captured -> %s (mask=%016llX watch=%02X)",
                    settings3DS.StereoProfiles[s_capProfile].Name,
                    (unsigned long long)mask, s_capWatch);
            }
            s_capProfile = -1;
        }
    }

    // match against the binds (first hit wins); no hit -> default (-1)
    int match = -1;
    for (int i = 0; i < settings3DS.StereoBindsCount; i++) {
        const S9xSettings3DS::SStereoBind *b = &settings3DS.StereoBinds[i];
        int watch = settings3DS.StereoWatchAddr >= 0
            ? Memory.RAM[settings3DS.StereoWatchAddr & 0x1FFFF] : -1;
        if (stereoSigBindMatches(sig, sig2, b->WatchVal < 0 ? -1 : watch,
                b->Sig, b->Mask, b->Sig2, b->Mask2, b->WatchVal) &&
            b->ProfileIdx < settings3DS.StereoProfilesCount) {
            match = b->ProfileIdx;
            break;
        }
    }

    // hysteresis: only switch after N consecutive frames of the same match
    if (match != s_stereoActiveIdx) {
        if (match == s_pendingIdx) {
            if (++s_stableFrames >= HYSTERESIS_FRAMES) {
                for (int i = 0; i < 5; i++) s_fromDepths[i] = GPU3DS.stereoLayerDepth[i];
                s_fromFx[0] = GPU3DS.stereoFade;
                s_fromFx[1] = GPU3DS.stereoHaze;
                s_fromFx[2] = GPU3DS.stereoBlur;
                s_fromFx[3] = GPU3DS.stereoFocusBack;
                s_fromFx[4] = GPU3DS.stereoFocusFront;
                s_stereoActiveIdx = match;
                s_lerpLeft = LERP_FRAMES;
                settings3DS.menuTabDirty[1] = true;   // TAB_SETTINGS: refresh 'This screen matches'
                log3dsWrite("[sig] profile -> %s",
                    match < 0 ? "Default" : settings3DS.StereoProfiles[match].Name);
            }
        } else {
            s_pendingIdx = match;
            s_stableFrames = 0;
        }
    } else {
        s_pendingIdx = match;
        s_stableFrames = 0;
    }

    // lerp the depths toward the active profile (pop -> glide)
    if (s_lerpLeft > 0) {
        s_lerpLeft--;
        S9xSettings3DS::SStereoProfile def;
        settings3dsStereoDefaultProfile(&def);
        const S9xSettings3DS::SStereoProfile *p =
            (s_stereoActiveIdx >= 0) ? &settings3DS.StereoProfiles[s_stereoActiveIdx] : &def;

        float t = 1.0f - (float)s_lerpLeft / LERP_FRAMES;
        float depths[5];
        for (int i = 0; i < 5; i++)
            depths[i] = s_fromDepths[i] + ((float)p->Depth[i] - s_fromDepths[i]) * t;
        float fx[5] = { (float)p->Fade, (float)p->Haze, (float)p->Blur,
                        (float)p->FocusBack, (float)p->FocusFront };
        for (int i = 0; i < 5; i++)
            fx[i] = s_fromFx[i] + (fx[i] - s_fromFx[i]) * t;
        settings3dsStereoApplyValues(depths, fx[0], fx[1], fx[2], fx[3], fx[4],
            p->EdgeMode);
    }
}

const char *settings3dsGetAppVersion(const char *prefix, const char *suffix) {
    static char version[64];

    if (VERSION_MICRO > 0) {
        snprintf(version, sizeof(version), "%s%d.%d.%d%s", prefix, VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, suffix != NULL ? suffix : "");
    } else {
        snprintf(version, sizeof(version), "%s%d.%d%s", prefix, VERSION_MAJOR, VERSION_MINOR, suffix != NULL ? suffix : "");
    }

    return version;
}
