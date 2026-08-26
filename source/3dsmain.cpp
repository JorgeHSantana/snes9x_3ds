#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>

#include <unistd.h>
#include <dirent.h>

#include <3ds.h>

#include "snes9x.h"
#include "cheats.h"
#include "memmap.h"

#include "3dsutils.h"
#include "3dssettings.h"
#include "3dslog.h"
#include "3dstimer.h"
#include "3dsexit.h"
#include "3dsconfig.h"
#include "3dsfiles.h"
#include "3dsinput.h"
#include "3dssound.h"
#include "3dsmsu.h"
#include "3dsrewind.h"
#include "3dsmsu_ndsp.h"
#include "3dsgpu.h"
#include "3dsimpl.h"
#include "3dsui.h"
#include "3dsui_notif.h"
#include "3dslcd.h"
#include "3dsui_img.h"
#include "3dsmenu.h"

inline std::string operator "" _s(const char* s, size_t length) {
    return std::string(s, length);
}

char romFileName[NAME_MAX + 1];
// Dev probe (armed by sdmc:/menuprobe.txt next to autoboot.txt): while the
// file exists, the emulator does an automatic menu round-trip every ~10s —
// the full pause/resume path (settings3dsUpdate included) with no input —
// to reproduce the "layers break after touching settings" bug.
static int  g_menuProbeCountdown = 0;
static bool g_menuProbeActive = false;
static bool g_menuProbeArmed = false;

bool slotLoaded = false;

const char* hotkeysData[HOTKEYS_COUNT][3];

static bool cfgFileAvailable[2]; // global config, game config
static u32 lastLoadedRomCRC = 0;
static const DirectoryEntry* selectedEntry = nullptr;

// static globals to prevent heap fragmentation and speed up menu access.
// note: not thread-safe, but safe here due to sequential menu/emulator execution
static std::vector<SMenuTab> menuTabs;
static std::vector<DirectoryEntry> entries;

// file menu scroll offset per directory level: pushed on the way down, restored on the way back up
static std::vector<int> fileMenuScrollStack;

static const int hotkeyDisplayOrder[HOTKEYS_COUNT] = {
    HOTKEY_OPEN_MENU,
    HOTKEY_FAST_FORWARD_TOGGLE,
    HOTKEY_FAST_FORWARD_HOLD,
    HOTKEY_REWIND_HOLD,
    HOTKEY_SWAP_CONTROLLERS,
    HOTKEY_SCREENSHOT,
    HOTKEY_QUICK_SAVE,
    HOTKEY_QUICK_LOAD,
    HOTKEY_SAVE_SLOT_NEXT,
    HOTKEY_SAVE_SLOT_PREV
};

extern SCheatData Cheat;

bool ResetHotkeyIfNecessary(int index, bool cpadBindingEnabled) {
    if (!cpadBindingEnabled)
        return false;

    ::ButtonMapping<1>& val = settings3DS.UseGlobalEmuControlKeys ? settings3DS.GlobalButtonHotkeys[index] : settings3DS.ButtonHotkeys[index];
    if (val.MappingBitmasks[0] == static_cast<int>(KEY_CPAD_UP) ||
        val.MappingBitmasks[0] == static_cast<int>(KEY_CPAD_DOWN) ||
        val.MappingBitmasks[0] == static_cast<int>(KEY_CPAD_LEFT) ||
        val.MappingBitmasks[0] == static_cast<int>(KEY_CPAD_RIGHT)) {
        val.SetSingleMapping(0);
        return true;
    }
    return false;
}

//----------------------------------------------------------------------
// Menu options
//----------------------------------------------------------------------

namespace {
    template <typename T>
    bool CheckAndUpdate( T& oldValue, const T& newValue ) {
        if (oldValue != newValue) {
            settings3DS.isDirty = true;
            oldValue = newValue;
            return true;
        }
        return false;
    }

    bool CheckAndUpdateToggle( bool& oldValue, const int& newValue ) {
        return CheckAndUpdate(oldValue, static_cast<bool>(newValue));
    }

    void AddMenuDialogOption(std::vector<SMenuItem>& items, int value, const std::string& text, const std::string& description = ""_s) {
        items.emplace_back(nullptr, MenuItemType::Action, text, description, value);
    }

    void AddMenuDisabledOption(std::vector<SMenuItem>& items, const std::string& text, int value = -1) {
        items.emplace_back(nullptr, MenuItemType::Disabled, text, ""_s, value);
    }

    void AddMenuHeader1(std::vector<SMenuItem>& items, const std::string& text) {
        items.emplace_back(nullptr, MenuItemType::Header1, text, ""_s);
    }

    void AddMenuHeader2(std::vector<SMenuItem>& items, const std::string& text) {
        // breathing room before every section header, unless the previous
        // item already provides it (a manual blank line or another header)
        if (!items.empty()) {
            const SMenuItem& prev = items.back();
            bool alreadySpaced =
                (prev.Type == MenuItemType::Disabled && prev.Text.empty()) ||
                prev.Type == MenuItemType::Header1;
            if (!alreadySpaced)
                items.emplace_back(nullptr, MenuItemType::Disabled, ""_s, ""_s);
        }
        items.emplace_back(nullptr, MenuItemType::Header2, text, ""_s);
    }

    void AddMenuCheckbox(std::vector<SMenuItem>& items, const std::string& text, int value, std::function<void(int)> callback) {
        items.emplace_back(callback, MenuItemType::Checkbox, text, ""_s, value);
    }

    void AddMenuRadio(std::vector<SMenuItem>& items, const std::string& text, int value, std::function<void(int)> callback) {
        items.emplace_back(callback, MenuItemType::Radio, text, ""_s, value);
    }

    // focusZoneCue: depth gauges dim their value when it leaves the stereo
    // focus zone (the layer will receive Depth Blur)
    void AddMenuGauge(std::vector<SMenuItem>& items, const std::string& text, int min, int max, int value, std::function<void(int)> callback, bool showValue = false, bool focusZoneCue = false) {
        items.emplace_back(callback, MenuItemType::Gauge, text, focusZoneCue ? "2"_s : (showValue ? "1"_s : ""_s), value, min, max);
    }

    void AddMenuPicker(std::vector<SMenuItem>& items, const std::string& text, const std::string& description, const std::vector<SMenuItem>& options, int value, int dialogType, bool showSelectedOptionInMenu, std::function<void(int)> callback) {
        items.emplace_back(callback, MenuItemType::Picker, text, ""_s, value, showSelectedOptionInMenu ? 1 : 0, 0, description, options, dialogType);
    }
}

std::vector<SMenuItem> makePickerOptions(const std::vector<std::string>& options) {
    std::vector<SMenuItem> items;
    items.reserve(options.size());

    for (size_t i = 0; i < options.size(); i++) {
        AddMenuDialogOption(items, static_cast<int>(i), options[i], ""_s);
    }

    return items;
}

std::vector<SMenuItem> makeOptionsForResetConfig() {
    std::vector<SMenuItem> items;
    items.reserve(4);

    AddMenuDialogOption(items, 0, "None"_s, ""_s);

    if (cfgFileAvailable[0]) {
        AddMenuDialogOption(items, 1, "Global"_s, "settings.cfg"_s);
    }
     
    if (cfgFileAvailable[1]) {
        char gameConfigFilename[128];
        char basename[NAME_MAX + 1];
        utils3dsGetBasename(Memory.ROMFilename, basename, sizeof(basename), false);

        if (strlen(basename) > 38) {
            snprintf(gameConfigFilename, sizeof(gameConfigFilename), "%.38s..%s", basename, ".cfg");
        } else {
            snprintf(gameConfigFilename, sizeof(gameConfigFilename), "%s%s", basename, ".cfg");
        }

        AddMenuDialogOption(items, 2, "Game"_s, gameConfigFilename);
    }

    if (cfgFileAvailable[0] && cfgFileAvailable[1]) {
        AddMenuDialogOption(items, 3, "Both"_s, ""_s);
    }
    
    return items;
}

const std::vector<SMenuItem>& makeOptionsForOk() {
    static std::vector<SMenuItem> items;

    if (items.empty()) {
        items.reserve(1);
        const char* options[] = { "OK" };
        AddMenuDialogOption(items, 0, options[0], ""_s);
    }

    return items;
}

const std::vector<SMenuItem>& makeOptionsForGameThumbnail() {
    static std::vector<SMenuItem> items;

    if (items.empty()) {
        const char* options[] = { "None", "Boxart", "Title", "Gameplay" };
        int count = MAX_THUMB_TYPES + 1;
        items.reserve(count);
        
        AddMenuDialogOption(items, 0, options[0], "");
        
        for (int i = 1; i < count; i++) {
            char typeName[32];
            snprintf(typeName, sizeof(typeName), "%s", options[i]);
            typeName[0] = (char)(tolower(typeName[0]));

            if (file3dsThumbnailsAvailableByType(typeName)) {
                AddMenuDialogOption(items, i, options[i], "");
            } else {
                AddMenuDisabledOption(items, options[i]);
            }
        }
    }

    return items;
}

std::vector<SMenuItem> makeOptionsForFileMenu(std::vector<FileMenuOption>& options, const char* selectedFileName) {
    std::vector<SMenuItem> items;
    options.clear();

    // set default Directory
    if (strcmp(settings3DS.defaultDir, file3dsGetCurrentDir()) != 0) {
        AddMenuDialogOption(items, options.size(), "Set current directory as default", "");
        options.push_back(FileMenuOption::SetDefaultDir);
    }

    // reset default Directory
    if (settings3DS.defaultDir[0]) {
        char label[64];
        char dirStr[PATH_MAX];
        snprintf(dirStr, sizeof(dirStr), "%s", settings3DS.defaultDir);
        
        size_t len = strlen(dirStr);
        if (len > 28) {
            snprintf(label, sizeof(label), "...%s", dirStr + (len - 28));
        } else {
            snprintf(label, sizeof(label), "%s", dirStr);
        }

        AddMenuDialogOption(items, options.size(), "Reset default directory", label);
        options.push_back(FileMenuOption::ResetDefaultDir);
    }

    // rebuild cache
    char cachePath[PATH_MAX];
    file3dsGetCurrentDirCacheName(cachePath, sizeof(cachePath));
    
    if (IsFileExists(cachePath)) {
        char optionTitle[128];
        const char* dateStr = file3dsGetCurrentDirCacheDate();

        if (dateStr && dateStr[0]) {
            snprintf(optionTitle, sizeof(optionTitle), "Refresh ROM List (cached: %s)", dateStr);
        } else {
            snprintf(optionTitle, sizeof(optionTitle), "Refresh ROM List");
        }
        
        AddMenuDialogOption(items, options.size(), optionTitle, "");
        options.push_back(FileMenuOption::RescanDir);
    }

    // random game
    if (file3dsGetCurrentDirRomCount() > 1) {
        AddMenuDialogOption(items, options.size(), "Select random game in current directory", "");
        options.push_back(FileMenuOption::RandomGame);
    }

    // delete Game
    if (selectedFileName && selectedFileName[0] != '\0') {
        AddMenuDialogOption(items, options.size(), "Delete selected game", "");
        options.push_back(FileMenuOption::DeleteGame);
    }

    return items;
}

bool confirmDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, const std::string& title, const std::string& message, bool fade, bool hideAfter) {
    int result = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, message, Themes[static_cast<int>(settings3DS.Theme)].dialogColorWarn, makePickerOptions({ "Yes", "No" }), 1, fade);

    if (hideAfter) {
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs, fade);
    }

    return result == 0;
}

// 3DS Mode (issue #16): New = 804 MHz + L2 cache, Old = 268 MHz without.
// PTMSYSM drives both bits explicitly; osSetSpeedupEnable is the fallback.
static void apply3dsMode(int mode)
{
    if (!settings3DS.isNew3DS) return;
    if (R_SUCCEEDED(ptmSysmInit())) {
        PTMSYSM_ConfigureNew3DSCPU(mode != 0 ? 3 : 0);   // bit0 clock, bit1 L2
        ptmSysmExit();
    } else {
        osSetSpeedupEnable(mode != 0);
    }
}

void makeEmulatorMenu(std::vector<SMenuItem>& items, std::vector<SMenuTab>& menuTabs, int& currentMenuTab) {
    items.clear();

    if (settings3DS.isRomLoaded) {
        AddMenuHeader1(items, "CURRENT GAME"_s);
        items.emplace_back([](int val) {        
            GPU3DS.emulatorState = EMUSTATE_EMULATE;
        }, MenuItemType::Action, "  Resume"_s, ""_s);


        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab;
            bool isDialog = false;
            bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Reset Console", "This will restart the game. Are you sure?", true, true);

            if (confirmed) {
                impl3dsResetConsole();
                GPU3DS.emulatorState = EMUSTATE_EMULATE;
            }
        }, MenuItemType::Action, "  Reset"_s, ""_s);

        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab;
            bool isDialog = false;
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "ROM Info", menu3dsGetRomInfo(), Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, makeOptionsForOk(), -1, true, 10);
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        }, MenuItemType::Action, "  ROM Info"_s, ""_s);

        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab;
            bool isDialog = false;
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Screenshot", "Saving screenshot...", Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, std::vector<SMenuItem>());

            char path[PATH_MAX];

            if (impl3dsTakeScreenshot(path, sizeof(path), true))
            {
                char message[PATH_MAX];
                const size_t maxPathLen = sizeof(message) - strlen("Screenshot saved to ") - 1;
                snprintf(message, sizeof(message), "Screenshot saved to %.*s", (int)maxPathLen, path);
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Screenshot", message, Themes[static_cast<int>(settings3DS.Theme)].dialogColorSuccess, makeOptionsForOk(), -1, false);
            }
            else
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Screenshot", "Failed to save screenshot!", Themes[static_cast<int>(settings3DS.Theme)].dialogColorWarn, makeOptionsForOk(), -1, false);
                        
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);

        }, MenuItemType::Action, "  Take Screenshot"_s, ""_s);

        AddMenuHeader2(items, "Save and Load"_s);
        AddMenuCheckbox(items, "  Create screenshot when saving"_s, settings3DS.SaveStateScreenshots,
            []( int val ) {
                bool wasEnabled = settings3DS.SaveStateScreenshots;
                if (CheckAndUpdateToggle( settings3DS.SaveStateScreenshots, val )) {
                    if (settings3DS.SaveStateScreenshots) {
                        impl3dsEnsureStateScreenshotDir();
                    } else if (wasEnabled) {
                        impl3dsDeleteStateScreenshots();
                    }
                }
            });
        items.emplace_back(nullptr, MenuItemType::Textarea, "  (disabling removes existing savestate screenshots)"_s, ""_s);
        AddMenuDisabledOption(items, ""_s);

        char slotInfo[32];

        for (int slot = 1; slot <= SAVESLOTS_MAX; ++slot) {
            bool hasState = impl3dsSlotHasState(slot);
            bool isCurrent = (slot == settings3DS.CurrentSaveSlot);
            RadioState state = hasState
                ? (isCurrent ? RADIO_ACTIVE_CHECKED : RADIO_ACTIVE)
                : (isCurrent ? RADIO_INACTIVE_CHECKED : RADIO_INACTIVE);
            snprintf(slotInfo, sizeof(slotInfo), "  Save Slot #%d", slot);

            AddMenuRadio(items, slotInfo, state,
                [slot, &menuTabs, &currentMenuTab](int) {
                    SMenuTab dialogTab;
                    bool isDialog = false;
                    bool result;

                    if (impl3dsHasBrokenAudioStateSignature()) {
                        char tag[32];
                        char path[PATH_MAX], ext[16];
                        snprintf(tag, sizeof(tag), "save-menu slot=%d", slot);
                        snprintf(ext, sizeof(ext), ".%d.frz", slot);
                        file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ext, "savestates");
                        impl3dsLogBrokenAudioSignatureContext(tag, path);

                        bool forceSave = confirmDialog(
                            dialogTab, isDialog, currentMenuTab, menuTabs,
                            "Savestates",
                            "Possible SPC audio issue detected. Resuming game briefly and trying again is recommended. Save anyway?",
                            true, false
                        );

                        if (!forceSave) {
                            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
                            return;
                        }
                    }

                    bool stateUsed = impl3dsSlotHasState(slot);

                    if (stateUsed) {
                        char confirmMessage[64];
                        snprintf(confirmMessage, sizeof(confirmMessage), "Are you sure to overwrite save slot #%d?", slot);
                        bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Savestates", confirmMessage, true, false);

                        if (!confirmed) {
                            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);

                            return;
                        }
                    }
                    
                    char statusMessage[64];
                    snprintf(statusMessage, sizeof(statusMessage), "Saving into slot #%d", slot);

                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Savestates", statusMessage, Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, std::vector<SMenuItem>(), -1, !stateUsed);
                    result = impl3dsSaveStateSlot(slot);

                    if (!result) {
                        snprintf(statusMessage, sizeof(statusMessage), "Saving into slot #%d failed.", slot);
                        menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Savestates", statusMessage, Themes[static_cast<int>(settings3DS.Theme)].dialogColorWarn, makeOptionsForOk(), -1, false);
                        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
                    }
                    else
                    {
                        if (settings3DS.SaveStateScreenshots) {
                            char screenshotPath[PATH_MAX];
                            screenshot.type = SCREENSHOT_SAVESTATE;
                            screenshot.slot = slot;
                            impl3dsTakeScreenshot(screenshotPath, sizeof(screenshotPath), true);
                        }

                        snprintf(statusMessage, sizeof(statusMessage), "Saving into slot #%d completed.", slot);
                        menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Savestates", statusMessage, Themes[static_cast<int>(settings3DS.Theme)].dialogColorSuccess, makeOptionsForOk(), -1, false);
                        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
                        CheckAndUpdate( settings3DS.CurrentSaveSlot, slot );
                    }
                }
            );
        }
        AddMenuDisabledOption(items, ""_s);

        for (int slot = 1; slot <= SAVESLOTS_MAX; ++slot) {
            bool hasState = impl3dsSlotHasState(slot);
            snprintf(slotInfo, sizeof(slotInfo), "  Load Slot #%d", slot);

            items.emplace_back([slot, &menuTabs, &currentMenuTab](int val) {
                // Fence the mixer out while msu1_restore closes/reopens files
                // (same fence impl3dsQuickSaveLoad uses). Safe here: menu entry
                // already resumed mixing, so no drain is active to nest with.
                snd3dsDrainMixing();
                bool result = impl3dsLoadStateSlot(slot);
                snd3dsResumeMixing();
                if (!result) {
                    SMenuTab dialogTab;
                    bool isDialog = false;
                    
                    char errorMessage[64];
                    snprintf(errorMessage, sizeof(errorMessage), "Unable to load slot #%d!", slot);

                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Savestate failure", errorMessage, Themes[static_cast<int>(settings3DS.Theme)].dialogColorWarn, makeOptionsForOk());
                    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
                } else {
                    // current slot changed -> rebuild so the checkmark moves on reopen
                    if (CheckAndUpdate( settings3DS.CurrentSaveSlot, slot ))
                        menu3dsMarkTabDirty(TAB_EMULATOR);
                    slotLoaded = true;
                    GPU3DS.emulatorState = EMUSTATE_EMULATE;
                }
            }, hasState ? MenuItemType::Action : MenuItemType::Disabled, slotInfo, ""_s);
        }
        AddMenuDisabledOption(items, ""_s);

    }

    AddMenuHeader1(items, "APPEARANCE"_s);

    const char* gameThumbnailMessage = 
        "Thumbnail type. Download latest *.cache files from\n"
        "github.com/matbo87/snes9x_3ds-assets and place\n"
        "them into 3ds/snes9x_3ds/thumbnails on your SD card.";

    AddMenuPicker(items, "  Game Thumbnail"_s, gameThumbnailMessage, makeOptionsForGameThumbnail(), static_cast<int>(settings3DS.GameThumbnailType), DIALOG_TYPE_INFO, true,
        []( int val ) { 
            if (!CheckAndUpdate(settings3DS.GameThumbnailType, static_cast<Setting::ThumbnailMode>(val))) {
                return;
            }
            
            img3dsSetThumbMode();
        });

    std::vector<std::string>themeNames;

    for (int i = 0; i < TOTALTHEMECOUNT; i++) {
        themeNames.emplace_back(std::string(Themes[i].Name));
    }

    AddMenuPicker(items, "  Theme"_s, "The theme used for the user interface."_s, makePickerOptions(themeNames), static_cast<int>(settings3DS.Theme), DIALOG_TYPE_INFO, true,
        []( int val ) { CheckAndUpdate(settings3DS.Theme, static_cast<Setting::Theme>(val)); });


    AddMenuPicker(items, "  Font"_s, "The font used for the user interface."_s, makePickerOptions({"Tempesta", "Ronda", "Arial"}), static_cast<int>(settings3DS.Font), DIALOG_TYPE_INFO, true,
        []( int val ) { if ( CheckAndUpdate( settings3DS.Font, static_cast<Setting::Font>(val) ) ) { ui3dsSetFont(); } });

    AddMenuPicker(items, "  Game Screen"_s, "Play your games on top or bottom screen"_s, makePickerOptions({"Top", "Bottom"}), settings3DS.GameScreen, DIALOG_TYPE_INFO, true,
        [&menuTabs, &currentMenuTab]( int val ) { 
            if (!CheckAndUpdate(settings3DS.GameScreen, (gfxScreen_t)val)) {
                return;
            }

            SMenuTab dialogTab;
            bool isDialog = false;
            
            menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTabs);
            
            // screen swap requires framebuffer format update on both screens
            menu3dsSetScreenDirty(true, true);
            ui3dsSetScreenLayout();
            menu3dsMarkTabDirty(TAB_EMULATOR);
            if (settings3DS.isRomLoaded) {
                menu3dsMarkTabDirty(TAB_SETTINGS);
            }

            log3dsWrite("screen swapped");
        });

    // 3D on/off and intensity are the physical slider's job: 0 = exact 2D,
    // anything above scales the effect analogically up to the full range.

    // 3DS Mode (issue #16): one switch for clock + L2 cache. Old 3DS/2DS
    // hardware has nothing to configure, so the option only shows on New.
    if (settings3DS.isNew3DS) {
        AddMenuPicker(items, "  3DS Mode"_s,
            "New: 804 MHz CPU + L2 cache - full speed\n(recommended). Old: 268 MHz, no L2 - useful\nto preview how a game runs on an Old 3DS."_s,
            makePickerOptions({"Old 3DS (268 MHz)", "New 3DS (804 MHz + L2)"}), settings3DS.Overclock, DIALOG_TYPE_INFO, true,
            []( int val ) {
                if (CheckAndUpdate(settings3DS.Overclock, val))
                    apply3dsMode(val);
            });
    }

    AddMenuDisabledOption(items, ""_s);
    AddMenuHeader1(items, "REWIND"_s);

    AddMenuPicker(items, "  Recording"_s,
        "Records gameplay in the background so the timeline\ncan jump back to any recent moment. Disable to\nreclaim the memory and skip the captures."_s,
        makePickerOptions({"Disabled", "Enabled"}), settings3DS.RewindEnabled, DIALOG_TYPE_INFO, true,
        []( int val ) { CheckAndUpdate(settings3DS.RewindEnabled, val); });

    AddMenuPicker(items, "  Max History"_s,
        "How far back the timeline reaches. Maximum depends\non the console and the game: up to ~1.5 min on\nNew 3DS (0.5s steps), ~1 min on Old 3DS (2s steps)."_s,
        makePickerOptions({"30 seconds", "1 minute", "Maximum"}), settings3DS.RewindMaxWindow, DIALOG_TYPE_INFO, true,
        []( int val ) { CheckAndUpdate(settings3DS.RewindMaxWindow, val); });

    AddMenuPicker(items, "  Capture Patience"_s,
        "How long a due capture may wait for an idle frame.\nThe idleness bar lowers as the wait grows, and at\nthe limit the capture happens regardless."_s,
        makePickerOptions({"1 second", "2 seconds", "4 seconds", "8 seconds"}), settings3DS.RewindMaxWait, DIALOG_TYPE_INFO, true,
        []( int val ) { CheckAndUpdate(settings3DS.RewindMaxWait, val); });

    AddMenuPicker(items, "  Resume Countdown"_s,
        "The 3..2..1 shown before play resumes from a rewound\nmoment, so your hands can get back to the controller."_s,
        makePickerOptions({"Off", "250 ms steps", "500 ms steps", "1 s steps"}), settings3DS.RewindCountdown, DIALOG_TYPE_INFO, true,
        []( int val ) { CheckAndUpdate(settings3DS.RewindCountdown, val); });

    if (settings3DS.isRomLoaded) {
        // B inside the timeline returns here (docs/rewind-v2-spec.md)
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            if (rewind3dsCount() == 0) {
                SMenuTab dialogTab;
                bool isDialog = false;
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Rewind",
                    settings3DS.RewindEnabled
                        ? "No rewind history yet. Play for a moment\nand try again."
                        : "Rewind is disabled. Enable it in this tab\nto record gameplay.",
                    Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, makeOptionsForOk(), -1, false);
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
                return;
            }
            rewind3dsRequestTimelineFromMenu();
            GPU3DS.emulatorState = EMUSTATE_EMULATE;
        }, MenuItemType::Action, "  Open Timeline"_s, ""_s);
    }

    AddMenuDisabledOption(items, ""_s);

    AddMenuHeader1(items, "OTHERS"_s);

    AddMenuCheckbox(items, "  Enable Logging (use when issues occur)"_s, settings3DS.LogFileEnabled,
        []( int val ) { CheckAndUpdateToggle( settings3DS.LogFileEnabled, val ); });
    std::string logfileInfo = "  Creates a session log in \"3ds/snes9x_3ds\". Restart required";
    AddMenuDisabledOption(items, logfileInfo);
    AddMenuDisabledOption(items, ""_s);

    if (cfgFileAvailable[0] || cfgFileAvailable[1]) {
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            char resetConfigDescription[NAME_MAX + 1];
            snprintf(
                resetConfigDescription, sizeof(resetConfigDescription), 
                "Restore default settings%s.", 
                (cfgFileAvailable[1] ? " and/or remove current game config" : "")
            );
            
            SMenuTab dialogTab;
            bool isDialog = false;
            int option = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Reset config"_s, resetConfigDescription, Themes[static_cast<int>(settings3DS.Theme)].dialogColorWarn, makeOptionsForResetConfig());
            
            // "None" selected or B pressed
            if (option <= 0) {
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);

                return;
            }

            // 1=Global, 2=Game, 3=Both
            bool resetGlobal = (option == 1 || option == 3);
            bool resetGame   = (option == 2 || option == 3);

            if (resetGlobal) {
                settings3dsResetGlobalDefaults();
                cfgFileAvailable[0] = false;

                // config reset may change GameScreen — both screens need framebuffer format update
                menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTabs);
                menu3dsSetScreenDirty(true, true);
                ui3dsSetFont(); 
                ui3dsSetScreenLayout();
            }

            if (resetGame) {
                settings3dsResetGameDefaults();
                cfgFileAvailable[1] = false;
            }

            settings3dsUpdate(resetGame);
            settings3DS.isDirty = true;

            // mark all tabs dirty
            for (int i = 0; i < TAB_DIRTY_COUNT; i++)
                settings3DS.menuTabDirty[i] = true;                
        }, MenuItemType::Action, "  Reset Config"_s, ""_s);
    }

    AddMenuPicker(items, "  Quit Emulator"_s, "Are you sure you want to quit?", makePickerOptions({ "Yes", "No" }), 1, DIALOG_TYPE_WARN, false,
        []( int val ) { if ( val == 0 ) { GPU3DS.emulatorState = EMUSTATE_END; } });

    AddMenuDisabledOption(items, ""_s);
    std::string info = std::string(settings3dsGetAppVersion("  Snes9x for 3DS v")) + " \x0b7 github.com/JorgeHSantana/snes9x_3ds";
    AddMenuDisabledOption(items, info);
}

const std::vector<SMenuItem>& makeOptionsForOnScreenDisplay() {
    static std::vector<SMenuItem> items;

    if (items.empty()) {
        items.reserve(4);
        AddMenuDialogOption(items, static_cast<int>(Setting::AssetMode::None), "None"_s,              ""_s);
        AddMenuDialogOption(items, static_cast<int>(Setting::AssetMode::Default), "Standard"_s,          "Uses _default.png, falls back to built-in"_s);
        AddMenuDialogOption(items, static_cast<int>(Setting::AssetMode::Adaptive), "Adaptive"_s,         "Uses <game>.png, falls back to Standard"_s);
        AddMenuDialogOption(items, static_cast<int>(Setting::AssetMode::CustomOnly), "Custom Only"_s,    "Uses <game>.png only, no fallback"_s);
    }
    
    return items;
}

std::vector<SMenuItem> makeOptionsForStretch() {
    std::vector<SMenuItem> items;
    items.reserve(6);

    AddMenuDialogOption(items, static_cast<int>(Setting::ScreenStretch::None), "No Stretch"_s,                   "Pixel Perfect (256x224)"_s);
    AddMenuDialogOption(items, static_cast<int>(Setting::ScreenStretch::Aspect_4_3), "4:3 Aspect"_s,             "Stretch width only to 298"_s);
    AddMenuDialogOption(items, static_cast<int>(Setting::ScreenStretch::CrtAspect), "CRT Aspect"_s,              "Stretch width only to 292 (8:7 PAR)"_s);
    AddMenuDialogOption(items, static_cast<int>(Setting::ScreenStretch::Fit_4_3), "4:3 Fit"_s,                   "Stretch to 320x240"_s);
    AddMenuDialogOption(items, static_cast<int>(Setting::ScreenStretch::Fit_8_7), "8:7 Fit"_s,                   "Stretch to 274x240"_s);

    if (settings3DS.GameScreen == GFX_TOP) {
        AddMenuDialogOption(items, static_cast<int>(Setting::ScreenStretch::Full), "Fullscreen"_s,               "Stretch to 400x240");
    }

    return items;
}

std::vector<SMenuItem> makeOptionsForEnhancedResolution() {
    std::vector<SMenuItem> items;
    items.reserve(3);

    // "2x Screen" (wide 800px panel) only exists on a wide-capable device's top screen.
    if (gpu3dsIsWideAvailable() && settings3DS.GameScreen == GFX_TOP) {
        AddMenuDialogOption(items, static_cast<int>(Setting::EnhancedResolution::Off),        "Off"_s);
        AddMenuDialogOption(items, static_cast<int>(Setting::EnhancedResolution::Standard),   "Standard"_s,   "Slightly sharper, keeps 3D"_s);
        AddMenuDialogOption(items, static_cast<int>(Setting::EnhancedResolution::Wide), "2x Screen"_s,  "Most detail, disables 3D"_s);
    } else {
        AddMenuDialogOption(items, static_cast<int>(Setting::EnhancedResolution::Off),      "Off"_s);
        AddMenuDialogOption(items, static_cast<int>(Setting::EnhancedResolution::Standard), "On"_s);
    }

    return items;
}


const std::vector<SMenuItem>& makeOptionsForButtonMapping() {
    static std::vector<SMenuItem> items;

    if (items.empty()) {
        items.reserve(9);
        
        AddMenuDialogOption(items, 0,                      "-"_s);
        AddMenuDialogOption(items, SNES_A_MASK,            "SNES A Button"_s);
        AddMenuDialogOption(items, SNES_B_MASK,            "SNES B Button"_s);
        AddMenuDialogOption(items, SNES_X_MASK,            "SNES X Button"_s);
        AddMenuDialogOption(items, SNES_Y_MASK,            "SNES Y Button"_s);
        AddMenuDialogOption(items, SNES_TL_MASK,           "SNES L Button"_s);
        AddMenuDialogOption(items, SNES_TR_MASK,           "SNES R Button"_s);
        AddMenuDialogOption(items, SNES_SELECT_MASK,       "SNES SELECT Button"_s);
        AddMenuDialogOption(items, SNES_START_MASK,        "SNES START Button"_s);
    }
    
    return items;
}

std::vector<SMenuItem> makeOptionsFor3DSButtonMapping() {
    std::vector<SMenuItem> items;
    items.reserve(17);

    AddMenuDialogOption(items, 0,                                   "-"_s);
    
	if(settings3DS.isNew3DS) {        
        AddMenuDialogOption(items, static_cast<int>(KEY_ZL),            "ZL Button"_s);
        AddMenuDialogOption(items, static_cast<int>(KEY_ZR),            "ZR Button"_s);
    }

    if ((!settings3DS.UseGlobalButtonMappings && !settings3DS.BindCirclePad) || (settings3DS.UseGlobalButtonMappings && !settings3DS.GlobalBindCirclePad)) {
        AddMenuDialogOption(items, static_cast<int>(KEY_CPAD_UP),            "Circle Pad Up"_s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CPAD_DOWN),            "Circle Pad Down"_s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CPAD_LEFT),            "Circle Pad Left"_s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CPAD_RIGHT),            "Circle Pad Right"_s);
    }

	if(settings3DS.isNew3DS) {
        AddMenuDialogOption(items, static_cast<int>(KEY_CSTICK_UP),            "C-stick Up"_s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CSTICK_DOWN),            "C-stick Down"_s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CSTICK_LEFT),            "C-stick Left"_s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CSTICK_RIGHT),            "C-stick Right"_s);
    }

    AddMenuDialogOption(items, static_cast<int>(KEY_A),             "3DS A Button"_s);
    AddMenuDialogOption(items, static_cast<int>(KEY_B),             "3DS B Button"_s);
    AddMenuDialogOption(items, static_cast<int>(KEY_X),             "3DS X Button"_s);
    AddMenuDialogOption(items, static_cast<int>(KEY_Y),             "3DS Y Button"_s);
    AddMenuDialogOption(items, static_cast<int>(KEY_L),             "3DS L Button"_s);
    AddMenuDialogOption(items, static_cast<int>(KEY_R),             "3DS R Button"_s);

    return items;
}

const std::vector<SMenuItem>& makeOptionsForFrameRate() {
    static std::vector<SMenuItem> items;
    if (items.empty()) {
        items.reserve(2);
        AddMenuDialogOption(items, static_cast<int>(Setting::Framerate::UseRomRegion), "Auto (Game Default)"_s, ""_s);
        AddMenuDialogOption(items, static_cast<int>(Setting::Framerate::ForceFps60),   "Force 60 FPS"_s, ""_s);
    }
    return items;
}

const std::vector<SMenuItem>& makeOptionsForFrameSync() {
    static std::vector<SMenuItem> items;
    if (items.empty()) {
        items.reserve(2);
        AddMenuDialogOption(items, static_cast<int>(Setting::FrameSync::VBlank), "VBlank Sync"_s, ""_s);
        AddMenuDialogOption(items, static_cast<int>(Setting::FrameSync::Sleep),  "Sleep Sync"_s, ""_s);
    }
    return items;
}

const std::vector<SMenuItem>& makeOptionsForAutoSaveSRAMDelay() {
    static std::vector<SMenuItem> items;
    if (items.empty()) {
        items.reserve(4);
        AddMenuDialogOption(items, 1, "1 second"_s, ""_s);
        AddMenuDialogOption(items, 2, "10 seconds"_s, ""_s);
        AddMenuDialogOption(items, 3, "60 seconds"_s, ""_s);
        AddMenuDialogOption(items, 4, "Disabled"_s,   ""_s);
    }
    return items;
}

const std::vector<SMenuItem>& makeOptionsForInFramePaletteChanges() {
    static std::vector<SMenuItem> items;
    if (items.empty()) {
        items.reserve(3);
        AddMenuDialogOption(items, 1, "Enabled"_s,          "Best (slower)"_s);
        AddMenuDialogOption(items, 2, "Disabled Style 1"_s, "Faster than \"Enabled\" (start palette)"_s);
        AddMenuDialogOption(items, 3, "Disabled Style 2"_s, "Faster than \"Enabled\" (final palette)"_s);
    }
    return items;
}

const std::vector<SMenuItem>& makeOptionsForScreenFilter() {
    static std::vector<SMenuItem> items;
    if (items.empty()) {
        items.reserve(3);
        AddMenuDialogOption(items, static_cast<int>(Setting::ScreenFilter::Sharp), "Sharp"_s, "Crisp pixels"_s);
        AddMenuDialogOption(items, static_cast<int>(Setting::ScreenFilter::Smooth), "Smooth"_s, "Soft edges"_s);
        AddMenuDialogOption(items, static_cast<int>(Setting::ScreenFilter::Balanced), "Balanced"_s, "Crisp + soft blend"_s);
    }
    return items;
}


// which profile the stereo gauges edit: -1 = Default (the flat fields)
static int s_stereoEditIdx = -1;
void settingsResetStereo3D();
void settingsLoadStereo3D();
void settingsSaveStereo3D();
void settingsSaveStereo3DDefault();

// tiny copy helper for the .3d <-> .3d.bak snapshots (files are a few KB)
static bool stereoCopyFile(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }

    char buf[1024];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;

    fclose(in);
    return fclose(out) == 0 && ok;
}

static int *stereoEditDepth(int layer) {
    if (s_stereoEditIdx >= 0 && s_stereoEditIdx < settings3DS.StereoProfilesCount)
        return &settings3DS.StereoProfiles[s_stereoEditIdx].Depth[layer];
    return &settings3DS.StereoDepth[layer];
}
static int *stereoEditField(int which) {
    if (s_stereoEditIdx >= 0 && s_stereoEditIdx < settings3DS.StereoProfilesCount) {
        S9xSettings3DS::SStereoProfile *p = &settings3DS.StereoProfiles[s_stereoEditIdx];
        switch (which) {
            case 0: return &p->Fade;      case 1: return &p->Haze;
            case 2: return &p->Blur;      case 3: return &p->FocusBack;
            case 4: return &p->FocusFront; default: return &p->EdgeMode;
        }
    }
    switch (which) {
        case 0: return &settings3DS.StereoFade;      case 1: return &settings3DS.StereoHaze;
        case 2: return &settings3DS.StereoBlur;      case 3: return &settings3DS.StereoFocusBack;
        case 4: return &settings3DS.StereoFocusFront; default: return &settings3DS.StereoEdgeMode;
    }
}

void makeOptionMenu(std::vector<SMenuItem>& items, std::vector<SMenuTab>& menuTabs, int& currentMenuTab) {
    items.clear();

    AddMenuHeader1(items, "GENERAL SETTINGS"_s);
    AddMenuHeader2(items, "Video"_s);
    AddMenuPicker(items, "  Scaling"_s, "Sets how the picture fills the screen. Stretched modes\npair well with Enhanced Resolution for a sharper look."_s, makeOptionsForStretch(), static_cast<int>(settings3DS.ScreenStretch), DIALOG_TYPE_INFO, true,
                  []( int val ) { 
                    if (CheckAndUpdate( settings3DS.ScreenStretch, static_cast<Setting::ScreenStretch>(val) )) { 
                        settings3dsApplyScreenStretch(); 
                        menu3dsSetScreenDirty(); 
                    } 
                });
    
    AddMenuPicker(items, "  Scale Filter"_s, "Applies when the game screen is stretched/zoomed in.\n\"Sharp\" may distort details,\n\"Balanced\" has minor performance impact."_s,
        makeOptionsForScreenFilter(), static_cast<int>(settings3DS.ScreenFilter), DIALOG_TYPE_INFO, true,
        []( int val ) {
            if (CheckAndUpdate(settings3DS.ScreenFilter, static_cast<Setting::ScreenFilter>(val))) {
                menu3dsSetScreenDirty();
            }
        });


    bool wideOptionAvailable = gpu3dsIsWideAvailable() && settings3DS.GameScreen == GFX_TOP;
    // preserve wide setting when switching game screen to bottom screen,
    // show it as Standard/On in picker
    int enhancedResolutionValue = static_cast<int>(settings3DS.EnhancedResolution);
    if (!wideOptionAvailable && settings3DS.EnhancedResolution == Setting::EnhancedResolution::Wide)
        enhancedResolutionValue = static_cast<int>(Setting::EnhancedResolution::Standard);

    AddMenuPicker(items, "  Enhanced Resolution"_s,
        wideOptionAvailable
            ? "Renders at higher resolution for a sharper, more\ndetailed picture. Recommended when stretched modes\nare used. 2x Screen has most performance impact."_s
            : "Sharpens the picture slightly, when stretched modes are used.\nLittle effect at \"No Stretch\"."_s,
        makeOptionsForEnhancedResolution(), enhancedResolutionValue, DIALOG_TYPE_INFO, true,
        []( int val ) {
            if (CheckAndUpdate( settings3DS.EnhancedResolution, static_cast<Setting::EnhancedResolution>(val) )) {
                GPU3DSExt.render2x.dirty = true;
                // 2x Screen takes over from 3D; rebuild so the 3D row shows/hides
                menu3dsMarkTabDirty(TAB_EMULATOR);
                menu3dsSetScreenDirty(true, true);
            }
        });

    AddMenuCheckbox(items, "  Crop & Overscan"_s, settings3DS.CropEnabled,
        []( int val ) {
            bool wasShown = settings3DS.CropEnabled;
            if (CheckAndUpdateToggle(settings3DS.CropEnabled, val)) {
                bool isShown = settings3DS.CropEnabled;
                if (isShown) {
                    settings3DS.CropTop = 8;
                    settings3DS.CropBottom = 8;
                    settings3DS.Overscan = true;
                } else {
                    settings3DS.CropTop = 0;
                    settings3DS.CropBottom = 0;
                    settings3DS.Overscan = false;
                }
                menu3dsSetScreenDirty();
                if (wasShown != isShown)
                    menu3dsMarkTabDirty(TAB_SETTINGS);
            }
        });

    if (settings3DS.CropEnabled) {
        AddMenuGauge(items, "  Crop Top Scanlines"_s, 0, 32, settings3DS.CropTop,
                        []( int val ) { if (CheckAndUpdate(settings3DS.CropTop, val)) menu3dsSetScreenDirty(); }, true);
        AddMenuGauge(items, "  Crop Bottom Scanlines"_s, 0, 32, settings3DS.CropBottom,
                        []( int val ) { if (CheckAndUpdate(settings3DS.CropBottom, val)) menu3dsSetScreenDirty(); }, true);
        AddMenuCheckbox(items, "  Overscan (zoom to fit height)"_s, settings3DS.Overscan,
            []( int val ) { if (CheckAndUpdateToggle(settings3DS.Overscan, val)) menu3dsSetScreenDirty(); });
    }

        
    AddMenuDisabledOption(items, ""_s);
    AddMenuHeader2(items, "On-Screen Display"_s);

    AddMenuGauge(items, "  Scanlines"_s, 0, SCANLINE_INTENSITY_MAX, settings3DS.ScanlineIntensity,
        []( int val ) {
            if (CheckAndUpdate(settings3DS.ScanlineIntensity, val)) {
                img3dsUpdateScanlineTexture();
                menu3dsSetScreenDirty();
            }
        });

    AddMenuPicker(items, "  Game Screen Overlay"_s, "506x256px recommended for Auto-Fit Bezel support.\npath = \"/3ds/snes9x3ds/overlays/\".\nTrimmed filename (e.g. Axelay.png) or _default.png."_s, 
        makeOptionsForOnScreenDisplay(), static_cast<int>(settings3DS.GameOverlay), DIALOG_TYPE_INFO, true,
                  []( int val ) {
                    bool wasShown = settings3DS.GameOverlay != Setting::AssetMode::None;
                    if (CheckAndUpdate( settings3DS.GameOverlay, static_cast<Setting::AssetMode>(val) )) {
                        impl3dsUpdateUiAssets();
                        menu3dsSetScreenDirty();
                        
                        bool isShown = settings3DS.GameOverlay != Setting::AssetMode::None;
                        if (wasShown != isShown)
                            menu3dsMarkTabDirty(TAB_SETTINGS);
                    }
                }
            );

    if (settings3DS.GameOverlay != Setting::AssetMode::None) {
        AddMenuCheckbox(items, "  Auto-Fit Bezel (based on \"Video Scaling\")", settings3DS.GameOverlayAutoFit,
            []( int val ) { if (CheckAndUpdateToggle( settings3DS.GameOverlayAutoFit, val )) menu3dsSetScreenDirty(); });
    }



    AddMenuPicker(items, "  Game Screen BG"_s, "Max 448x256px image behind game, shifted by 3D.\npath = \"/3ds/snes9x3ds/backgrounds/game_screen/\".\nTrimmed filename (e.g. Axelay.png) or _default.png."_s,
        makeOptionsForOnScreenDisplay(), static_cast<int>(settings3DS.GameScreenBg), DIALOG_TYPE_INFO, true,
                    []( int val ) {
                        bool wasShown = settings3DS.GameScreenBg != Setting::AssetMode::None;
                        if (CheckAndUpdate(settings3DS.GameScreenBg, static_cast<Setting::AssetMode>(val))) {
                            impl3dsUpdateUiAssets();
                            menu3dsSetScreenDirty();
                            
                            bool isShown = settings3DS.GameScreenBg != Setting::AssetMode::None;
                            if (wasShown != isShown)
                                menu3dsMarkTabDirty(TAB_SETTINGS);
                        }
                    }
                );

    if (settings3DS.GameScreenBg != Setting::AssetMode::None) {
        AddMenuGauge(items, "  Game Screen BG Opacity"_s, 1, OPACITY_STEPS, settings3DS.GameScreenBgOpacity,
                        []( int val ) { if (CheckAndUpdate( settings3DS.GameScreenBgOpacity, val )) menu3dsSetScreenDirty(); });
    }

    AddMenuPicker(items, "  Second Screen BG"_s, "Max 400x240px image shown on the second screen.\npath = \"/3ds/snes9x3ds/backgrounds/second_screen/\".\nTrimmed filename (e.g. Axelay.png) or _default.png."_s,
        makeOptionsForOnScreenDisplay(), static_cast<int>(settings3DS.SecondScreenBg), DIALOG_TYPE_INFO, true,
                    []( int val ) {
                        bool wasShown = settings3DS.SecondScreenBg != Setting::AssetMode::None;
                        if (CheckAndUpdate(settings3DS.SecondScreenBg, static_cast<Setting::AssetMode>(val))) {
                            
                            bool isShown = settings3DS.SecondScreenBg != Setting::AssetMode::None;
                            if (wasShown != isShown)
                                menu3dsMarkTabDirty(TAB_SETTINGS);
                        }
                    }
                );

    if (settings3DS.SecondScreenBg != Setting::AssetMode::None) {
        AddMenuGauge(items, "  Second Screen BG Opacity"_s, 1, OPACITY_STEPS, settings3DS.SecondScreenBgOpacity,
                        []( int val ) { CheckAndUpdate( settings3DS.SecondScreenBgOpacity, val ); });
    }

    AddMenuCheckbox(items, "  Show FPS", settings3DS.ShowFPS,
        []( int val ) { CheckAndUpdateToggle( settings3DS.ShowFPS, val ); });

    AddMenuDisabledOption(items, ""_s);

    AddMenuHeader1(items, "GAME-SPECIFIC SETTINGS"_s);
    AddMenuHeader2(items, "Video"_s);
    AddMenuPicker(items, "  Frameskip"_s, "Try changing this if the game runs slow. Skipping frames helps it run faster, but less smooth."_s, 
        makePickerOptions({"Disabled", "Enabled (max 1 frame)", "Enabled (max 2 frames)", "Enabled (max 3 frames)", "Enabled (max 4 frames)"}), settings3DS.MaxFrameSkips, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.MaxFrameSkips, val ); });
    
    AddMenuPicker(items, "  Framerate"_s, "PAL games run at 50 FPS by default.\nEnable 60 FPS override if needed."_s, makeOptionsForFrameRate(), static_cast<int>(settings3DS.Framerate), DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.Framerate, static_cast<Setting::Framerate>(val) ); });
    AddMenuPicker(items, "  Frame Sync"_s, "VBlank Sync is best for most games. If a game stutters\nor won't hold full speed, try Sleep Sync. On O3DS\nit helps demanding games like DKC2 run smoother."_s,
                  makeOptionsForFrameSync(), static_cast<int>(settings3DS.FrameSync), DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate(settings3DS.FrameSync, static_cast<Setting::FrameSync>(val)); });

    AddMenuPicker(items, "  In-Frame Palette Changes"_s, "Try changing this if some colors in the game look off."_s, makeOptionsForInFramePaletteChanges(), settings3DS.PaletteFix, DIALOG_TYPE_INFO, true,
                  []( int val ) {
                      // rebuild the tab: the ADVANCED SETTINGS section
                      // (Reduce Layer Draws) only exists while this is Enabled
                      if (CheckAndUpdate( settings3DS.PaletteFix, val ))
                          menu3dsMarkTabDirty(TAB_SETTINGS);
                  });

    AddMenuCheckbox(items, "  Mode 7 Smoothing"_s, settings3DS.Mode7BilinearFilter,
        []( int val ) { CheckAndUpdateToggle( settings3DS.Mode7BilinearFilter, val ); });

    if (Settings.MSU1)
    {
        AddMenuPicker(items, "  MSU-1 Video FPS"_s, "Caps how often MSU-1 FMV frames are shown.\nLower values lighten rendering and can hide flicker."_s, makePickerOptions({"Off", "40 FPS", "30 FPS", "20 FPS", "15 FPS", "10 FPS"}),
                    settings3DS.Msu1VideoFps, DIALOG_TYPE_INFO, true,
                    []( int val ) { CheckAndUpdate( settings3DS.Msu1VideoFps, val ); });
    }

    AddMenuDisabledOption(items, ""_s);
    
    AddMenuHeader2(items, "Audio"_s);
    // Gauges: 25% per step, midpoint (4) = 100%.
    AddMenuGauge(items, "  SNES Volume"_s, 0, SND3DS_VOLUME_MAX,
                settings3DS.UseGlobalVolume ? settings3DS.GlobalVolume : settings3DS.Volume,
                []( int val ) {
                    if (settings3DS.UseGlobalVolume)
                        CheckAndUpdate( settings3DS.GlobalVolume, val );
                    else
                        CheckAndUpdate( settings3DS.Volume, val );
                });
    if (Settings.MSU1)
    {
        AddMenuGauge(items, "  MSU-1 Volume"_s, 0, 8,
                    settings3DS.UseGlobalVolume ? settings3DS.GlobalMsu1Volume : settings3DS.Msu1Volume,
                    []( int val ) {
                        if (settings3DS.UseGlobalVolume)
                            CheckAndUpdate( settings3DS.GlobalMsu1Volume, val );
                        else
                            CheckAndUpdate( settings3DS.Msu1Volume, val );
                    });
    }
    AddMenuCheckbox(items, "  Apply volume to all games"_s, settings3DS.UseGlobalVolume,
                []( int val )
                {
                    CheckAndUpdateToggle( settings3DS.UseGlobalVolume, val );
                    if (settings3DS.UseGlobalVolume)
                    {
                        settings3DS.GlobalVolume = settings3DS.Volume;
                        // only an MSU-1 game may promote its MSU-1 volume to global
                        if (Settings.MSU1)
                            settings3DS.GlobalMsu1Volume = settings3DS.Msu1Volume;
                    }
                    else
                    {
                        settings3DS.Volume = settings3DS.GlobalVolume;
                        settings3DS.Msu1Volume = settings3DS.GlobalMsu1Volume;
                    }
                });

    AddMenuPicker(items, "  SNES Audio Buffer"_s, "Higher values can reduce audio crackling, especially on Old 3DS, at the cost of more audio latency."_s, makePickerOptions({"Low", "Normal", "High"}), settings3DS.AudioBuffer, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.AudioBuffer, val ); });

    AddMenuDisabledOption(items, ""_s);

    AddMenuHeader2(items, "Save Data"_s);

    AddMenuCheckbox(items, "  Automatically save state on exit, load state on start"_s, settings3DS.AutoSavestate,
        []( int val ) { CheckAndUpdateToggle( settings3DS.AutoSavestate, val ); });
    items.emplace_back(nullptr, MenuItemType::Textarea, "  (creates an *.auto.frz file inside \"savestates\" directory)"_s, ""_s);

    AddMenuPicker(items, "  SRAM Auto-Save Delay"_s, "Periodically writes SRAM to the SD card.\nEach write can briefly freeze the game.\nDisabled still saves on exit/sleep."_s, makeOptionsForAutoSaveSRAMDelay(), settings3DS.SRAMSaveInterval, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.SRAMSaveInterval, val ); });
    AddMenuCheckbox(items, "  Force SRAM Write on Pause"_s, settings3DS.ForceSRAMWriteOnPause,
                    []( int val ) { CheckAndUpdateToggle( settings3DS.ForceSRAMWriteOnPause, val ); });

    items.emplace_back(nullptr, MenuItemType::Textarea, "  (some games like Yoshi's Island require this)"_s, ""_s);

    AddMenuDisabledOption(items, ""_s);

    // Only meaningful while In-Frame Palette Changes is Enabled (the deferral path).
    if (settings3DS.PaletteFix == 1)
    {
        AddMenuHeader1(items, "ADVANCED SETTINGS"_s);

        AddMenuHeader2(items, "Reduce Layer Draws on Palette Changes"_s);

        static const char *deferBgNames[3] = { "  BG1", "  BG2", "  BG3" };
        for (int bg = LAYER_BG0; bg <= LAYER_BG2; bg++) {
            AddMenuCheckbox(items, deferBgNames[bg], (settings3DS.PaletteDeferBgMask >> bg) & 1,
                [bg]( int val ) {
                    u8 newMask = val
                        ? (settings3DS.PaletteDeferBgMask | (1 << bg))
                        : (settings3DS.PaletteDeferBgMask & ~(1 << bg));
                    CheckAndUpdate( settings3DS.PaletteDeferBgMask, newMask );
                });
        }
        items.emplace_back(nullptr, MenuItemType::Textarea, "  Can speed up games like Top Gear that change colors"_s, ""_s);
        items.emplace_back(nullptr, MenuItemType::Textarea, "  mid-frame by drawing a layer fewer times."_s, ""_s);

        AddMenuDisabledOption(items, ""_s);
    }

    AddMenuHeader1(items, "3D STEREOSCOPIC SETTINGS"_s);
    items.emplace_back(nullptr, MenuItemType::Textarea, "  Saved to /3ds/snes9x_3ds/stereo3d/<game>.3d (shareable)."_s, ""_s);
    AddMenuDisabledOption(items, ""_s);

    if (gpu3dsIs3DAvailable()) {
        AddMenuHeader2(items, "Scene Profiles"_s);

        if (s_stereoEditIdx >= settings3DS.StereoProfilesCount)
            s_stereoEditIdx = -1;

        std::vector<std::string> profNames;
        profNames.push_back("Default");
        for (int i = 0; i < settings3DS.StereoProfilesCount; i++)
            profNames.push_back(settings3DS.StereoProfiles[i].Name);
        profNames.push_back("+ New Profile");

        AddMenuPicker(items, "  Editing Profile"_s,
            "The Depth/Focus/Effects gauges below edit the selected profile.\n'+ New Profile' creates a copy of the current selection."_s,
            makePickerOptions(profNames), s_stereoEditIdx + 1, DIALOG_TYPE_INFO, true,
            [&menuTabs, &currentMenuTab]( int val ) {
                int newCount = settings3DS.StereoProfilesCount;
                if (val == newCount + 1) {
                    // + New Profile: copy of the current selection
                    if (newCount < STEREO_PROFILES_MAX) {
                        S9xSettings3DS::SStereoProfile *p = &settings3DS.StereoProfiles[newCount];
                        if (s_stereoEditIdx >= 0) {
                            *p = settings3DS.StereoProfiles[s_stereoEditIdx];
                        } else {
                            for (int i = 0; i < 5; i++) p->Depth[i] = settings3DS.StereoDepth[i];
                            p->Fade = settings3DS.StereoFade; p->Haze = settings3DS.StereoHaze;
                            p->Blur = settings3DS.StereoBlur;
                            p->FocusBack = settings3DS.StereoFocusBack;
                            p->FocusFront = settings3DS.StereoFocusFront;
                            p->EdgeMode = settings3DS.StereoEdgeMode;
                        }
                        snprintf(p->Name, sizeof(p->Name), "Profile %d", (newCount + 1) & 0xFF);
                        settings3DS.StereoProfilesCount++;
                        s_stereoEditIdx = newCount;
                        settings3DS.isDirty = true;

                        // go straight into naming the new profile
                        SwkbdState swkbd;
                        char buf[16];
                        swkbdInit(&swkbd, SWKBD_TYPE_QWERTY, 1, sizeof(buf) - 1);
                        swkbdSetInitialText(&swkbd, p->Name);
                        swkbdSetHintText(&swkbd, "Profile name");
                        if (swkbdInputText(&swkbd, buf, sizeof(buf)) == SWKBD_BUTTON_CONFIRM && buf[0] != '\0') {
                            snprintf(p->Name, sizeof(p->Name), "%s", buf);
                        }
                    }
                } else {
                    s_stereoEditIdx = val - 1;
                }
                menu3dsMarkTabDirty(TAB_SETTINGS);
            });

        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            if (s_stereoEditIdx < 0) return;
            S9xSettings3DS::SStereoProfile *p = &settings3DS.StereoProfiles[s_stereoEditIdx];
            SwkbdState swkbd;
            char buf[16];
            swkbdInit(&swkbd, SWKBD_TYPE_QWERTY, 1, sizeof(buf) - 1);
            swkbdSetInitialText(&swkbd, p->Name);
            swkbdSetHintText(&swkbd, "Profile name");
            if (swkbdInputText(&swkbd, buf, sizeof(buf)) == SWKBD_BUTTON_CONFIRM && buf[0] != '\0') {
                snprintf(p->Name, sizeof(p->Name), "%s", buf);
                settings3DS.isDirty = true;
                menu3dsMarkTabDirty(TAB_SETTINGS);
            }
        }, MenuItemType::Action, "  Rename Profile"_s, ""_s);
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            if (s_stereoEditIdx < 0) return;
            SMenuTab dialogTab; bool isDialog = false;
            bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
                "Delete Profile", "Remove this profile and its screen binds?", true, true);
            if (!confirmed) return;
            int del = s_stereoEditIdx;
            for (int i = settings3DS.StereoBindsCount - 1; i >= 0; i--) {
                if (settings3DS.StereoBinds[i].ProfileIdx == del) {
                    for (int j = i; j < settings3DS.StereoBindsCount - 1; j++)
                        settings3DS.StereoBinds[j] = settings3DS.StereoBinds[j + 1];
                    settings3DS.StereoBindsCount--;
                } else if (settings3DS.StereoBinds[i].ProfileIdx > del) {
                    settings3DS.StereoBinds[i].ProfileIdx--;
                }
            }
            for (int i = del; i < settings3DS.StereoProfilesCount - 1; i++)
                settings3DS.StereoProfiles[i] = settings3DS.StereoProfiles[i + 1];
            settings3DS.StereoProfilesCount--;
            s_stereoEditIdx = -1;
            settings3DS.isDirty = true;
            menu3dsMarkTabDirty(TAB_SETTINGS);
        }, MenuItemType::Action, "  Delete Profile"_s, ""_s);
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab; bool isDialog = false;
            if (s_stereoEditIdx < 0) {
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Capture This Screen",
                    "Select or create a profile first - captures bind the\ncurrent screen to the profile being edited.",
                    Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, makeOptionsForOk(), -1, false);
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
                return;
            }
            settings3dsStereoArmCapture(s_stereoEditIdx);
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Capture This Screen",
                "After resuming, keep the game on this screen for ~5\nseconds. The screen will then switch to this profile\nautomatically whenever it appears.",
                Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, makeOptionsForOk(), -1, false);
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        }, MenuItemType::Action, "  Capture This Screen"_s, ""_s);
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab; bool isDialog = false;
            bool removed = settings3dsStereoReleaseScreen();
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Release This Screen",
                removed ? "This screen was unbound - it now uses the Default\nprofile again."
                        : "This screen has no profile bound to it.",
                Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, makeOptionsForOk(), -1, false);
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
            menu3dsMarkTabDirty(TAB_SETTINGS);
        }, MenuItemType::Action, "  Release This Screen"_s, ""_s);





        {
            char matchLine[48];
            snprintf(matchLine, sizeof(matchLine), "  This screen matches: %s", settings3dsStereoActiveName());
            items.emplace_back(nullptr, MenuItemType::Textarea, std::string(matchLine), ""_s);
        }
    }

    AddMenuHeader2(items, "Enable / Disable Layers"_s);

    static const char *layerNames[LAYER_BRIGHTNESS + 1] = { "  BG1", "  BG2", "  BG3", "  BG4", "  Sprites", "  Backdrop", "  Color Math", "  Brightness" };
    for (int layer = LAYER_BG0; layer <= LAYER_BRIGHTNESS; layer++) {
        AddMenuCheckbox(items, layerNames[layer], settings3DS.LayerEnabled[layer],
            [layer]( int val ) { CheckAndUpdateToggle( settings3DS.LayerEnabled[layer], val ); });
    }
    items.emplace_back(nullptr, MenuItemType::Textarea, "  Enable/Disable Layers is temporary diagnostic. Not saved."_s, ""_s);

    if (gpu3dsIs3DAvailable()) {
        AddMenuHeader2(items, "Depth"_s);

        static const char *stereoNames[5] = { "  BG1", "  BG2", "  BG3", "  BG4", "  Sprites" };
        for (int l = 0; l < 5; l++) {
            AddMenuGauge(items, stereoNames[l], -8, 8, *stereoEditDepth(l),
                [l]( int val ) { CheckAndUpdate( *stereoEditDepth(l), val ); }, true, true);
        }
        items.emplace_back(nullptr, MenuItemType::Textarea, "  + pops out of the screen, - sinks into it."_s, ""_s);

        AddMenuHeader2(items, "Focus"_s);
        AddMenuGauge(items, "  Back"_s, -8, 0, *stereoEditField(3),
            []( int val ) { CheckAndUpdate( *stereoEditField(3), val ); }, true);
        AddMenuGauge(items, "  Front"_s, 0, 8, *stereoEditField(4),
            []( int val ) { CheckAndUpdate( *stereoEditField(4), val ); }, true);
        items.emplace_back(nullptr, MenuItemType::Textarea, "  Layers inside the Back..Front zone are untouched; effects"_s, ""_s);
        items.emplace_back(nullptr, MenuItemType::Textarea, "  grow with the distance beyond it (gray depth values)."_s, ""_s);

        AddMenuHeader2(items, "Effects"_s);
        AddMenuGauge(items, "  Fade"_s, 0, 8, *stereoEditField(0),
            []( int val ) { CheckAndUpdate( *stereoEditField(0), val ); }, true);
        items.emplace_back(nullptr, MenuItemType::Textarea, "  Darkens layers behind the focus zone."_s, ""_s);
        AddMenuGauge(items, "  Haze"_s, 0, 8, *stereoEditField(1),
            []( int val ) { CheckAndUpdate( *stereoEditField(1), val ); }, true);
        items.emplace_back(nullptr, MenuItemType::Textarea, "  Fogs layers behind the focus zone."_s, ""_s);
        AddMenuGauge(items, "  Blur"_s, 0, 8, *stereoEditField(2),
            []( int val ) { CheckAndUpdate( *stereoEditField(2), val ); }, true);
        items.emplace_back(nullptr, MenuItemType::Textarea, "  Smudges layers outside the zone, back and front."_s, ""_s);
        items.emplace_back(nullptr, MenuItemType::Textarea, "  Enabling it may cause small image artifacts."_s, ""_s);
        AddMenuPicker(items, "  Edge Cleanup"_s,
            "The per-layer parallax corrupts a few columns at the screen\nedges. Trim narrows the game window (scale kept); Zoom crops\nthem away, absorbed by the stretch; Off shows the raw edges."_s,
            makePickerOptions({"Off", "Trim", "Zoom"}), *stereoEditField(5), DIALOG_TYPE_INFO, true,
            []( int val ) { CheckAndUpdate( *stereoEditField(5), val ); });
        items.emplace_back(nullptr, MenuItemType::Textarea, "  Hides the screen-edge columns disturbed by the 3D shifts."_s, ""_s);

        AddMenuDisabledOption(items, ""_s);
        AddMenuHeader2(items, "Tools"_s);
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab; bool isDialog = false;
            char info[512];
            settings3dsStereoMatchInfo(info, sizeof(info));
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Scene Matcher Info",
                info, Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo,
                makeOptionsForOk(), -1, false, 8);
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        }, MenuItemType::Action, "  Scene Matcher Info"_s, ""_s);
        items.emplace_back(nullptr, MenuItemType::Textarea, "  What the matcher sees on the screen you paused on."_s, ""_s);
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab; bool isDialog = false;
            bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
                "Set as Global Default",
                "Use this game's look (depths, focus, effects) as the\nstarting point for games without their own settings?", true, true);
            if (!confirmed) return;

            settingsSaveStereo3DDefault();
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Set as Global Default",
                "Saved to stereo3d/default.3d. Games without a .3d\nfile now start from this look.",
                Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, makeOptionsForOk(), -1, false);
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        }, MenuItemType::Action, "  Set as Global Default"_s, ""_s);
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab; bool isDialog = false;

            char curPath[PATH_MAX];
            file3dsGetRelatedPath(Memory.ROMFilename, curPath, sizeof(curPath), ".3d", "stereo3d");
            const char *curBase = strrchr(curPath, '/');
            curBase = curBase ? curBase + 1 : curPath;

            char dirPath[PATH_MAX];
            snprintf(dirPath, sizeof(dirPath), "%s/stereo3d", settings3DS.RootDir);

            std::vector<std::string> files;
            DIR *d = opendir(dirPath);
            if (d) {
                struct dirent *e;
                while ((e = readdir(d)) != NULL) {
                    if (e->d_name[0] == '.') continue;
                    size_t len = strlen(e->d_name);
                    if (len < 4 || strcasecmp(e->d_name + len - 3, ".3d") != 0) continue;
                    if (strcasecmp(e->d_name, "default.3d") == 0) continue;
                    if (strcmp(e->d_name, curBase) == 0) continue;
                    files.push_back(e->d_name);
                }
                closedir(d);
            }
            std::sort(files.begin(), files.end());

            if (files.empty()) {
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Copy 3D Settings From",
                    "No other game has 3D settings saved yet.",
                    Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, makeOptionsForOk(), -1, false);
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
                return;
            }

            std::vector<SMenuItem> fileItems;
            for (size_t i = 0; i < files.size(); i++)
                AddMenuDialogOption(fileItems, (int)i, files[i].substr(0, files[i].size() - 3), ""_s);

            int idx = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Copy 3D Settings From",
                "Import another game's look as this game's settings.",
                Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, fileItems, -1, true);
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
            if (idx < 0 || idx >= (int)files.size()) return;

            bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
                "Copy 3D Settings From",
                "Replace this game's 3D settings with that look?\nThis game's profiles will be removed.", true, true);
            if (!confirmed) return;

            // import only the global look: profiles/binds/watch are
            // per-game fingerprints and stay behind
            char srcPath[PATH_MAX * 2];
            snprintf(srcPath, sizeof(srcPath), "%s/%s", dirPath, files[idx].c_str());
            FILE *in = fopen(srcPath, "r");
            FILE *out = curPath[0] != '\0' ? fopen(curPath, "w") : NULL;
            if (in && out) {
                char line[96];
                while (fgets(line, sizeof(line), in)) {
                    if (strncmp(line, "PROFILE=", 8) == 0 ||
                        strncmp(line, "WATCH=", 6) == 0 ||
                        strncmp(line, "BIND=", 5) == 0)
                        break;
                    fputs(line, out);
                }
            }
            if (in) fclose(in);
            if (out) fclose(out);

            int savedWatch = settings3DS.StereoWatchAddr;   // game metadata, keep
            settingsLoadStereo3D();
            settings3DS.StereoWatchAddr = savedWatch;
            settings3dsStereoApplyDefault();
            settings3DS.isDirty = true;
            menu3dsMarkTabDirty(TAB_SETTINGS);
        }, MenuItemType::Action, "  Copy 3D Settings From..."_s, ""_s);
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab; bool isDialog = false;
            char path[PATH_MAX], bak[PATH_MAX];
            file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".3d", "stereo3d");
            // hidden sibling folder keeps the shareable stereo3d/ clean
            file3dsGetRelatedPath(Memory.ROMFilename, bak, sizeof(bak), ".3d", ".stereo3d-bak");

            settingsSaveStereo3D();
            bool ok = path[0] != '\0' && bak[0] != '\0' && stereoCopyFile(path, bak);
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Backup 3D Settings",
                ok ? "Snapshot saved. 'Restore 3D Settings Backup' brings\nit back at any time."
                   : "Could not write the backup file.",
                Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, makeOptionsForOk(), -1, false);
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        }, MenuItemType::Action, "  Backup 3D Settings"_s, ""_s);
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab; bool isDialog = false;
            char path[PATH_MAX], bak[PATH_MAX];
            file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".3d", "stereo3d");
            file3dsGetRelatedPath(Memory.ROMFilename, bak, sizeof(bak), ".3d", ".stereo3d-bak");

            if (bak[0] == '\0' || !IsFileExists(bak)) {
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Restore 3D Settings Backup",
                    "No backup found for this game yet.\nUse 'Backup 3D Settings' to create one.",
                    Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, makeOptionsForOk(), -1, false);
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
                return;
            }

            bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
                "Restore 3D Settings Backup",
                "This OVERWRITES all current 3D settings AND\nprofiles of this game with the snapshot. Restore?", true, true);
            if (!confirmed) return;

            if (path[0] != '\0' && stereoCopyFile(bak, path)) {
                settingsLoadStereo3D();
                settings3dsStereoApplyDefault();
                settings3DS.isDirty = true;
                menu3dsMarkTabDirty(TAB_SETTINGS);
            }
        }, MenuItemType::Action, "  Restore 3D Settings Backup"_s, ""_s);
        items.emplace_back(nullptr, MenuItemType::Textarea, "  Snapshot the current 3D setup before experimenting."_s, ""_s);
        items.emplace_back([&menuTabs, &currentMenuTab](int val) {
            SMenuTab dialogTab; bool isDialog = false;
            bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
                "Reset 3D Settings",
                "Delete ALL profiles and their screen binds, and\nrestore the Default profile to factory values?", true, true);
            if (!confirmed) return;

            settingsResetStereo3D();
            settings3dsStereoApplyDefault();
            settings3DS.isDirty = true;
            menu3dsMarkTabDirty(TAB_SETTINGS);
        }, MenuItemType::Action, "  Reset 3D Settings"_s, ""_s);
        items.emplace_back(nullptr, MenuItemType::Textarea, "  Deletes every profile and restores factory values."_s, ""_s);
    }








        AddMenuDisabledOption(items, ""_s);
};

void makeControlsMenu(std::vector<SMenuItem>& items, std::vector<SMenuTab>& menuTabs, int& currentMenuTab) {
    items.clear();

    const char *t3dsButtonNames[10];
    t3dsButtonNames[BTN3DS_A] = "3DS A Button";
    t3dsButtonNames[BTN3DS_B] = "3DS B Button";
    t3dsButtonNames[BTN3DS_X] = "3DS X Button";
    t3dsButtonNames[BTN3DS_Y] = "3DS Y Button";
    t3dsButtonNames[BTN3DS_L] = "3DS L Button";
    t3dsButtonNames[BTN3DS_R] = "3DS R Button";
    t3dsButtonNames[BTN3DS_ZL] = "3DS ZL Button";
    t3dsButtonNames[BTN3DS_ZR] = "3DS ZR Button";
    t3dsButtonNames[BTN3DS_SELECT] = "3DS SELECT Button";
    t3dsButtonNames[BTN3DS_START] = "3DS START Button";

    AddMenuHeader1(items, "EMULATOR INGAME FUNCTIONS"_s);


    AddMenuCheckbox(items, "  Apply hotkey mappings to all games"_s, settings3DS.UseGlobalEmuControlKeys,
                []( int val )
                {
                    CheckAndUpdateToggle( settings3DS.UseGlobalEmuControlKeys, val );
                    if (settings3DS.UseGlobalEmuControlKeys) {
                        for (int i = 0; i < HOTKEYS_COUNT; ++i)
                            settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0] = settings3DS.ButtonHotkeys[i].MappingBitmasks[0];
                    }
                    else {
                        for (int i = 0; i < HOTKEYS_COUNT; ++i)
                            settings3DS.ButtonHotkeys[i].MappingBitmasks[0] = settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0];
                    }
                });

    for (int displayIdx = 0; displayIdx < HOTKEYS_COUNT; ++displayIdx) {
        int i = hotkeyDisplayOrder[displayIdx];
        AddMenuPicker( items,  hotkeysData[i][1], hotkeysData[i][2], makeOptionsFor3DSButtonMapping(), 
            settings3DS.UseGlobalEmuControlKeys ? settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0] : settings3DS.ButtonHotkeys[i].MappingBitmasks[0], DIALOG_TYPE_INFO, true,
            [i]( int val ) {
                if (settings3DS.UseGlobalEmuControlKeys)
                    CheckAndUpdate( settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0], (u32)val );
                else
                    CheckAndUpdate( settings3DS.ButtonHotkeys[i].MappingBitmasks[0], (u32)val );
            }
        );
    }

    AddMenuDisabledOption(items, ""_s);

    AddMenuHeader1(items, "BUTTON CONFIGURATION"_s);
    AddMenuCheckbox(items, "  Apply button mappings to all games"_s, settings3DS.UseGlobalButtonMappings,
                []( int val )
                {
                    CheckAndUpdateToggle( settings3DS.UseGlobalButtonMappings, val );

                    if (settings3DS.UseGlobalButtonMappings) {
                        for (int i = 0; i < 10; i++)
                            for (int j = 0; j < 4; j++)
                                settings3DS.GlobalButtonMapping[i][j] = settings3DS.ButtonMapping[i][j];
                        settings3DS.GlobalBindCirclePad = settings3DS.BindCirclePad;
                    }
                    else {
                        for (int i = 0; i < 10; i++)
                            for (int j = 0; j < 4; j++)
                                settings3DS.ButtonMapping[i][j] = settings3DS.GlobalButtonMapping[i][j];
                        settings3DS.BindCirclePad = settings3DS.GlobalBindCirclePad;
                    }

                });
    AddMenuCheckbox(items, "  Apply rapid fire settings to all games"_s, settings3DS.UseGlobalTurbo,
                []( int val )
                {
                    CheckAndUpdateToggle( settings3DS.UseGlobalTurbo, val );
                    if (settings3DS.UseGlobalTurbo) {
                        for (int i = 0; i < 8; i++)
                            settings3DS.GlobalTurbo[i] = settings3DS.Turbo[i];
                    }
                    else {
                        for (int i = 0; i < 8; i++)
                            settings3DS.Turbo[i] = settings3DS.GlobalTurbo[i];
                    }
                });
    
    
    AddMenuHeader2(items, "Analog to Digital Type"_s);
    AddMenuCheckbox(items, "  Bind Circle Pad to D-Pad"_s, settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalBindCirclePad : settings3DS.BindCirclePad,
                  []( int val ) {
                    if (CheckAndUpdateToggle(settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalBindCirclePad : settings3DS.BindCirclePad, val)) {
                        // reset hotkeys that conflict with the new circle pad binding;
                        // the rebuild regenerates each hotkey picker's options and value
                        for (int i = 0; i < HOTKEYS_COUNT; ++i)
                            ResetHotkeyIfNecessary(i, val);
                        menu3dsMarkTabDirty(TAB_CONTROLS);
                    }
                });
    items.emplace_back(nullptr, MenuItemType::Textarea, "  (when disabled, Circle Pad is available for hotkeys)"_s, ""_s);
                
    for (size_t i = 0; i < 10; ++i) {
        // skip option for ZL and ZR button when device is O3DS/O2DS
        if ((i == BTN3DS_ZL || i == BTN3DS_ZR) && !settings3DS.isNew3DS) {
            continue;
        }

        std::string optionButtonName = std::string(t3dsButtonNames[i]);
        AddMenuHeader2(items, optionButtonName);

        for (size_t j = 0; j < 3; ++j) {
            AddMenuPicker( items, "  Maps to"_s, ""_s, makeOptionsForButtonMapping(), 
                settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalButtonMapping[i][j] : settings3DS.ButtonMapping[i][j],
                DIALOG_TYPE_INFO, true,
                [i, j]( int val ) {
                    if (settings3DS.UseGlobalButtonMappings)
                        CheckAndUpdate( settings3DS.GlobalButtonMapping[i][j], val );
                    else
                        CheckAndUpdate( settings3DS.ButtonMapping[i][j], val );
                }
            );
        }

        if (i < 8)
            AddMenuGauge(items, "  Rapid-Fire Speed"_s, 0, 10, 
                settings3DS.UseGlobalTurbo ? settings3DS.GlobalTurbo[i] : settings3DS.Turbo[i],
                [i]( int val )
                {
                    if (settings3DS.UseGlobalTurbo)
                        CheckAndUpdate( settings3DS.GlobalTurbo[i], val ); 
                    else
                        CheckAndUpdate( settings3DS.Turbo[i], val ); 
                });
        
    }
}

//-------------------------------------------------------
// Sets up all the cheats to be displayed in the menu.
//-------------------------------------------------------

void makeCheatMenu(std::vector<SMenuItem>& items)
{
    int cheatsActive = 0;

    items.clear();

    if (Cheat.num_cheats > 0) {
        items.reserve(Cheat.num_cheats + 1); 
    } else {
        items.reserve(1);
    }

    if (Cheat.num_cheats > 0) {
        AddMenuHeader1(items, "");

        char buffer[128]; 

        for (uint32 i = 0; i < static_cast<uint32>(MAX_CHEATS) && i < Cheat.num_cheats; i++) {
            std::string name = Cheat.c[i].name;
            if (utils3dsIsAllUppercase(Cheat.c[i].name)) {
                for (size_t j = 1; j < name.length(); j++) {
                    if (std::isalpha(name[j])) {
                        name[j] = std::tolower(name[j]);
                    }
                }
            }

            snprintf(buffer, sizeof(buffer), "  %s", Cheat.c[i].name);

            if (Cheat.c[i].enabled) {
                cheatsActive++;
            }

            items.emplace_back(
                nullptr, 
                MenuItemType::Checkbox, 
                buffer, 
                Cheat.c[i].cheat_code, 
                Cheat.c[i].enabled ? 1 : 0
            );
        }
    }
    else {
        char romName[NAME_MAX + 1];
        utils3dsGetTrimmedBasename(Memory.ROMFilename, romName, sizeof(romName), false);

        static char message[PATH_MAX];
        snprintf(message, sizeof(message),
            "\nNo cheats found for this game. To enable cheats, copy\n"
            "\"%s.chx\" (or *.cht) into folder \"%s\" on your sd card.\n"
            "\n\nGame-Genie and Pro Action Replay Codes are supported.\n"
            "Format for *.chx is [Y/N],[CheatCode],[Name].\n"
            "See %s for more info\n"
            "\n\nCheat collection (roughly tested): %s",
            romName,
            "3ds/snes9x_3ds/cheats",
            "github.com/matbo87/snes9x_3ds-assets",
            "https://github.com/matbo87/snes9x_3ds-assets/releases/download/v0.1.0/cheats.zip");

        items.emplace_back(nullptr, MenuItemType::Textarea, message, "");
    }

    menu3dsSetCheatsCount(items[0], cheatsActive, Cheat.num_cheats);
}

//----------------------------------------------------------------------
// Read/write all possible game specific settings.
//----------------------------------------------------------------------
bool settingsReadWriteFullListByGame(bool writeMode)
{
    char path[PATH_MAX];
    file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".cfg", "configs");
    
    if (path[0] == '\0') {
        return false;
    }
    
    BufferedFileWriter stream;

    if (writeMode) {
        if (!stream.open(path, "w"))
            return false;
    } else {
        if (!stream.open(path, "r"))
            return false;
        config3dsResetParseWarning();
    }

    char version[16];
    snprintf(version, sizeof(version), "%.1f", GAME_CONFIG_FILE_TARGET_VERSION);
    config3dsReadWriteString(stream, writeMode, "# v%s\n", "# v%15[^\n]\n", version);

    // if writing, we are definitely on the latest version
    // if reading, we parse what we just read into 'version'
    float detectedConfigVersion = writeMode 
        ? GAME_CONFIG_FILE_TARGET_VERSION 
        : config3dsGetVersionFromFile(true, version);

    config3dsReadWriteInt32(stream, writeMode, "# Do not modify this file or risk losing your settings.\n", NULL, 0, 0);

    // skip reading Framerate setting from older cfg files to avoid expected parse-mismatch warnings
    if (writeMode || detectedConfigVersion >= 1.2f) {
        config3dsReadWriteEnum(stream, writeMode, "Framerate=%d\n", &settings3DS.Framerate, 0, 1);
    }

    if (writeMode || detectedConfigVersion >= 1.4f) {
        config3dsReadWriteEnum(stream, writeMode, "CropEnabled=%d\n", &settings3DS.CropEnabled, 0, 1);
        config3dsReadWriteInt32(stream, writeMode, "CropTop=%d\n", &settings3DS.CropTop, 0, 32);
        config3dsReadWriteInt32(stream, writeMode, "CropBottom=%d\n", &settings3DS.CropBottom, 0, 32);
        config3dsReadWriteEnum(stream, writeMode, "Overscan=%d\n", &settings3DS.Overscan, 0, 1);
        config3dsReadWriteEnum(stream, writeMode, "FrameSync=%d\n", &settings3DS.FrameSync, 0, 1);
        config3dsReadWriteEnum(stream, writeMode, "Mode7BilinearFilter=%d\n", &settings3DS.Mode7BilinearFilter, 0, 1);
        config3dsReadWriteInt32(stream, writeMode, "AudioBuffer=%d\n", &settings3DS.AudioBuffer, 0, 2);
    }

    if (writeMode || detectedConfigVersion >= 1.5f) {
        config3dsReadWriteEnum(stream, writeMode, "EnhancedResolution=%d\n", &settings3DS.EnhancedResolution, 0, 2);
        config3dsReadWriteEnum(stream, writeMode, "PaletteDeferBgMask=%d\n", &settings3DS.PaletteDeferBgMask, 0, 7);
    }

    if (writeMode || detectedConfigVersion >= 1.9f) {
        config3dsReadWriteInt32(stream, writeMode, "Msu1Volume=%d\n", &settings3DS.Msu1Volume, 0, 8);
    }
    else if (detectedConfigVersion >= 1.6f) {
        // Interim MSU-1 builds wrote extra keys here; consume them so the
        // sequential parse stays aligned, then remap to the gauge scale.
        if (detectedConfigVersion < 1.7f)
            config3dsReadWriteInt32(stream, writeMode, "Msu1Enabled=%d\n", NULL, 0, 1);
        config3dsReadWriteInt32(stream, writeMode, "Msu1Volume=%d\n", &settings3DS.Msu1Volume, 0, 8);
        if (detectedConfigVersion >= 1.8f)
            settings3DS.Msu1Volume += 4;    // v1.8 scale was 0..4 = 100%..200%
    }

    config3dsReadWriteInt32(stream, writeMode, "Frameskips=%d\n", &settings3DS.MaxFrameSkips, 0, 4);
    if (writeMode || detectedConfigVersion >= 1.9f) {
        config3dsReadWriteInt32(stream, writeMode, "Vol=%d\n", &settings3DS.Volume, 0, SND3DS_VOLUME_MAX);
    } else {
        // Pre-gauge scale was 0..4 = 100%..200%; gauge midpoint 4 = 100%.
        config3dsReadWriteInt32(stream, writeMode, "Vol=%d\n", &settings3DS.Volume, 0, 4);
        settings3DS.Volume += 4;
    }
    config3dsReadWriteInt32(stream, writeMode, "PalFix=%d\n", &settings3DS.PaletteFix, 0, 3);

    config3dsReadWriteEnum(stream, writeMode, "AutoSavestate=%d\n", &settings3DS.AutoSavestate, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "SRAMInterval=%d\n", &settings3DS.SRAMSaveInterval, 0, 4);
    config3dsReadWriteEnum(stream, writeMode, "ForceSRAMWrite=%d\n", &settings3DS.ForceSRAMWriteOnPause, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "BindCirclePad=%d\n", &settings3DS.BindCirclePad, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "LastSaveSlot=%d\n", &settings3DS.CurrentSaveSlot, 0, 5);

    static const char *buttonName[10] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "SELECT","START"};
    char keyBuf[64];

    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 3; ++j) {
            snprintf(keyBuf, sizeof(keyBuf), "ButtonMap%s_%d=%%d\n", buttonName[i], j);
            config3dsReadWriteInt32(stream, writeMode, keyBuf, &settings3DS.ButtonMapping[i][j]);
        }
    }

    static const char *turboButtonName[8] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR"};
    for (int i = 0; i < 8; ++i) {
        snprintf(keyBuf, sizeof(keyBuf), "Turbo%s=%%d\n", turboButtonName[i]);
        config3dsReadWriteInt32(stream, writeMode, keyBuf, &settings3DS.Turbo[i], 0, 10);
    }

    // skip reading new hotkey schema from older cfg files and map legacy key names in-place
    bool usesLegacyHotkeys = !writeMode && detectedConfigVersion < 1.3f;
    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        if (!strlen(hotkeysData[i][0])) {
            continue;
        }

        const char* hotkeyName = hotkeysData[i][0];
        if (usesLegacyHotkeys) {
            if (i == HOTKEY_FAST_FORWARD_HOLD) {
                settings3DS.ButtonHotkeys[i].SetSingleMapping(0);
                continue;
            }

            if (i == HOTKEY_FAST_FORWARD_TOGGLE) {
                hotkeyName = "DisableFramelimitHold";
            }
        }

        snprintf(keyBuf, sizeof(keyBuf), "ButtonMapping%s_0=%%d\n", hotkeyName);
        config3dsReadWriteBitmask(stream, writeMode, keyBuf, &settings3DS.ButtonHotkeys[i].MappingBitmasks[0]);
    }

    return true;
}


//----------------------------------------------------------------------
// Read/write all possible global settings.
//----------------------------------------------------------------------
bool settingsReadWriteFullListGlobal(bool writeMode)
{
    char globalConfig[PATH_MAX];
    snprintf(globalConfig, sizeof(globalConfig), "%s/%s", settings3DS.RootDir, "settings.cfg");
    
    BufferedFileWriter stream;

    if (writeMode) {
        if (!stream.open(globalConfig, "w"))
            return false;
    } else {
        if (!stream.open(globalConfig, "r"))
            return false;
        config3dsResetParseWarning();
    }


    char version[16];
    snprintf(version, sizeof(version), "%.1f", GLOBAL_CONFIG_FILE_TARGET_VERSION);
    config3dsReadWriteString(stream, writeMode, "# v%s\n", "# v%15[^\n]\n", version);

    // if writing, we are definitely on the latest version
    // if reading, we parse what we just read into 'version'
    float detectedConfigVersion = writeMode 
        ? GLOBAL_CONFIG_FILE_TARGET_VERSION 
        : config3dsGetVersionFromFile(false, version);

    config3dsReadWriteInt32(stream, writeMode, "# Do not modify this file or risk losing your settings.\n", NULL, 0, 0);
    config3dsReadWriteEnum(stream, writeMode, "GameScreen=%d\n", &settings3DS.GameScreen, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "Theme=%d\n", &settings3DS.Theme, 0, TOTALTHEMECOUNT - 1);
    config3dsReadWriteEnum(stream, writeMode, "GameThumbnailType=%d\n", &settings3DS.GameThumbnailType, 0, 3);
    config3dsReadWriteEnum(stream, writeMode, "ScreenStretch=%d\n", &settings3DS.ScreenStretch, 0, 7);
    
    if (writeMode || detectedConfigVersion >= 1.6f) {
        config3dsReadWriteEnum(stream, writeMode, "SaveStateScreenshots=%d\n", &settings3DS.SaveStateScreenshots, 0, 1);
        config3dsReadWriteInt32(stream, writeMode, "ScanlineIntensity=%d\n", &settings3DS.ScanlineIntensity, 0, SCANLINE_INTENSITY_MAX);
    } else if (!writeMode) {
        const int legacyStretch = static_cast<int>(settings3DS.ScreenStretch);
        if (legacyStretch == 5) {
            settings3DS.ScreenStretch = Setting::ScreenStretch::Fit_4_3;
        } else if (legacyStretch == 7) {
            settings3DS.ScreenStretch = Setting::ScreenStretch::Full;
        }
    }

    config3dsReadWriteEnum(stream, writeMode, "GameOverlay=%d\n", &settings3DS.GameOverlay, 0, 3);
    config3dsReadWriteEnum(stream, writeMode, "GameOverlayAutoFit=%d\n", &settings3DS.GameOverlayAutoFit, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "SecondScreenBg=%d\n", &settings3DS.SecondScreenBg, 0, 3);
    config3dsReadWriteInt32(stream, writeMode, "SecondScreenBgOpacity=%d\n", &settings3DS.SecondScreenBgOpacity, 1, OPACITY_STEPS);
    config3dsReadWriteEnum(stream, writeMode, "GameScreenBg=%d\n", &settings3DS.GameScreenBg, 0, 3);
    config3dsReadWriteInt32(stream, writeMode, "GameScreenBgOpacity=%d\n", &settings3DS.GameScreenBgOpacity, 1, OPACITY_STEPS);
    config3dsReadWriteEnum(stream, writeMode, "Disable3DSlider=%d\n", &settings3DS.Disable3DSlider, 0, 1);

    if (writeMode || detectedConfigVersion >= 1.6f) {
        config3dsReadWriteEnum(stream, writeMode, "Intensity3D=%d\n", &settings3DS.Intensity3D, 0, 2);
    }

    if (writeMode || detectedConfigVersion >= 1.7f) {
        config3dsReadWriteInt32(stream, writeMode, "Msu1VideoFps=%d\n", &settings3DS.Msu1VideoFps, 0, 5);
    }

    // Gauge-era leftovers (v1.8-2.0 wrote different keys here) hit the parse
    // mismatch, keep defaults for the rest and self-heal on the next save.
    if (writeMode || detectedConfigVersion >= 2.1f) {
        config3dsReadWriteInt32(stream, writeMode, "GlobalMsu1Volume=%d\n", &settings3DS.GlobalMsu1Volume, 0, 8);
    }
    
    config3dsReadWriteEnum(stream, writeMode, "Font=%d\n", &settings3DS.Font, 0, 2);
    config3dsReadWriteEnum(stream, writeMode, "LogFileEnabled=%d\n", &settings3DS.LogFileEnabled, 0, 1);

    char formatBuf[64];
    snprintf(formatBuf, sizeof(formatBuf), "DefaultDir=%%%zu[^\n]\n", sizeof(settings3DS.defaultDir) - 1);
    config3dsReadWriteString(stream, writeMode, "DefaultDir=%s\n", formatBuf, settings3DS.defaultDir);
    snprintf(formatBuf, sizeof(formatBuf), "LastSelectedDir=%%%zu[^\n]\n", sizeof(settings3DS.lastSelectedDir) - 1);
    config3dsReadWriteString(stream, writeMode, "LastSelectedDir=%s\n", formatBuf, settings3DS.lastSelectedDir);
    snprintf(formatBuf, sizeof(formatBuf), "LastSelectedFilename=%%%zu[^\n]\n", sizeof(settings3DS.lastSelectedFilename) - 1);
    config3dsReadWriteString(stream, writeMode, "LastSelectedFilename=%s\n", formatBuf, settings3DS.lastSelectedFilename);

    if (writeMode || detectedConfigVersion >= 2.1f) {
        config3dsReadWriteInt32(stream, writeMode, "Vol=%d\n", &settings3DS.GlobalVolume, 0, SND3DS_VOLUME_MAX);
    } else {
        // Pre-gauge scale was 0..4 = 100%..200%; gauge midpoint 4 = 100%.
        config3dsReadWriteInt32(stream, writeMode, "Vol=%d\n", &settings3DS.GlobalVolume, 0, 4);
        settings3DS.GlobalVolume += 4;
    }
    config3dsReadWriteEnum(stream, writeMode, "BindCirclePad=%d\n", &settings3DS.GlobalBindCirclePad, 0, 1);

    static const char *buttonName[10] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "SELECT","START"};
    char keyBuf[64];

    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 3; ++j) {
            snprintf(keyBuf, sizeof(keyBuf), "ButtonMap%s_%d=%%d\n", buttonName[i], j);
            config3dsReadWriteInt32(stream, writeMode, keyBuf, &settings3DS.GlobalButtonMapping[i][j]);
        }
    }
    
    static const char *turboButtonName[8] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR"};
    for (int i = 0; i < 8; ++i) {
        snprintf(keyBuf, sizeof(keyBuf), "Turbo%s=%%d\n", turboButtonName[i]);
        config3dsReadWriteInt32(stream, writeMode, keyBuf, &settings3DS.GlobalTurbo[i], 0, 10);
    }

    // skip reading new hotkey schema from older cfg files and map legacy key names in-place
    bool usesLegacyGlobalHotkeys = !writeMode && detectedConfigVersion < 1.5f;
    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        if (!strlen(hotkeysData[i][0])) {
            continue;
        }

        const char* hotkeyName = hotkeysData[i][0];
        if (usesLegacyGlobalHotkeys) {
            if (i == HOTKEY_FAST_FORWARD_HOLD) {
                settings3DS.GlobalButtonHotkeys[i].SetSingleMapping(0);
                continue;
            }

            if (i == HOTKEY_FAST_FORWARD_TOGGLE) {
                hotkeyName = "DisableFramelimitHold";
            }
        }

        snprintf(keyBuf, sizeof(keyBuf), "ButtonMapping%s_0=%%d\n", hotkeyName);
        config3dsReadWriteBitmask(stream, writeMode, keyBuf, &settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0]);
    }

    config3dsReadWriteEnum(stream, writeMode, "ScreenFilter=%d\n", &settings3DS.ScreenFilter, 0, 2);

    config3dsReadWriteEnum(stream, writeMode, "UseGlobalButtonMappings=%d\n", &settings3DS.UseGlobalButtonMappings, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "UseGlobalTurbo=%d\n", &settings3DS.UseGlobalTurbo, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "UseGlobalVolume=%d\n", &settings3DS.UseGlobalVolume, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "UseGlobalEmuControlKeys=%d\n", &settings3DS.UseGlobalEmuControlKeys, 0, 1);

    config3dsReadWriteEnum(stream, writeMode, "ShowFPS=%d\n", &settings3DS.ShowFPS, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "Overclock=%d\n", &settings3DS.Overclock, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "RewindCountdown=%d\n", &settings3DS.RewindCountdown, 0, 3);
    config3dsReadWriteEnum(stream, writeMode, "RewindEnabled=%d\n", &settings3DS.RewindEnabled, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "RewindMaxWindow=%d\n", &settings3DS.RewindMaxWindow, 0, 2);
    config3dsReadWriteEnum(stream, writeMode, "RewindMaxWait=%d\n", &settings3DS.RewindMaxWait, 0, 3);

    return true;
}

//----------------------------------------------------------------------
// Stereoscopic 3D depths live in their own shareable files:
// sd:/3ds/snes9x_3ds/stereo3d/<rom>.3d (fallback: default.3d).
//----------------------------------------------------------------------
static const int stereoDepthDefault[5] = { 0, -2, -1, 0, 0 };
static const char *stereoDepthKeys[5] = { "BG1", "BG2", "BG3", "BG4", "OBJ" };

// Factory state: no profiles/binds, Default profile at its factory values.
// Keeps StereoWatchAddr - the WATCH byte is file metadata about the game,
// not a user setting, and it is painful to re-mine.
void settingsResetStereo3D()
{
    for (int i = 0; i < 5; i++)
        settings3DS.StereoDepth[i] = stereoDepthDefault[i];
    settings3DS.StereoFade = 0;
    settings3DS.StereoHaze = 0;
    settings3DS.StereoBlur = 0;
    settings3DS.StereoFocusBack = -1;
    settings3DS.StereoFocusFront = 1;
    settings3DS.StereoEdgeMode = 1;   // Trim
    settings3DS.StereoProfilesCount = 0;
    settings3DS.StereoBindsCount = 0;
    s_stereoEditIdx = -1;
}

void settingsLoadStereo3D()
{
    settingsResetStereo3D();
    settings3DS.StereoWatchAddr = -1;

    char path[PATH_MAX];
    file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".3d", "stereo3d");

    FILE *f = path[0] != '\0' ? fopen(path, "r") : NULL;
    bool fallback = false;
    if (!f) {
        snprintf(path, sizeof(path), "%s/stereo3d/default.3d", settings3DS.RootDir);
        f = fopen(path, "r");
        fallback = true;
    }
    if (!f) return;

    // keys route to the "current target": the flat default fields, or the
    // profile opened by the last PROFILE= line (issue #23)
    int *tDepth = settings3DS.StereoDepth;
    int *tFade = &settings3DS.StereoFade;
    int *tHaze = &settings3DS.StereoHaze;
    int *tBlur = &settings3DS.StereoBlur;
    int *tFB = &settings3DS.StereoFocusBack;
    int *tFF = &settings3DS.StereoFocusFront;
    int *tEdge = &settings3DS.StereoEdgeMode;

    char line[96], name[16];
    int v;
    unsigned long long sv, mv;
    while (fgets(line, sizeof(line), f)) {
        // default.3d only carries the global look: profiles/binds/watch are
        // per-game fingerprints and must never leak across games
        if (fallback && (strncmp(line, "PROFILE=", 8) == 0 ||
                         strncmp(line, "WATCH=", 6) == 0 ||
                         strncmp(line, "BIND=", 5) == 0))
            continue;
        if (sscanf(line, "PROFILE=%15[^\r\n]", name) == 1) {
            if (settings3DS.StereoProfilesCount < STEREO_PROFILES_MAX) {
                S9xSettings3DS::SStereoProfile *p =
                    &settings3DS.StereoProfiles[settings3DS.StereoProfilesCount++];
                snprintf(p->Name, sizeof(p->Name), "%s", name);
                for (int i = 0; i < 5; i++) p->Depth[i] = stereoDepthDefault[i];
                p->Fade = p->Haze = p->Blur = 0;
                p->FocusBack = -1; p->FocusFront = 1; p->EdgeMode = 1;
                tDepth = p->Depth; tFade = &p->Fade; tHaze = &p->Haze;
                tBlur = &p->Blur; tFB = &p->FocusBack; tFF = &p->FocusFront;
                tEdge = &p->EdgeMode;
            }
            continue;
        }
        if (sscanf(line, "WATCH=%llx", &sv) == 1) {
            settings3DS.StereoWatchAddr = (int)(sv & 0x1FFFF);
            continue;
        }
        unsigned long long sv2 = 0, mv2 = 0;
        int wv = -1;
        int bn = sscanf(line, "BIND=%15[^:]:%llx:%llx:%llx:%llx:%x", name, &sv, &mv, &sv2, &mv2, &wv);
        if (bn >= 3) {
            if (settings3DS.StereoBindsCount < STEREO_BINDS_MAX) {
                for (int i = 0; i < settings3DS.StereoProfilesCount; i++) {
                    if (strcmp(settings3DS.StereoProfiles[i].Name, name) == 0) {
                        S9xSettings3DS::SStereoBind *b =
                            &settings3DS.StereoBinds[settings3DS.StereoBindsCount++];
                        b->Sig = sv; b->Mask = mv;
                        b->Sig2 = sv2; b->Mask2 = mv2;
                        b->WatchVal = (bn >= 6 && wv <= 0xFF) ? wv : -1;
                        b->ProfileIdx = i;
                        break;
                    }
                }
            }
            continue;
        }
        for (int i = 0; i < 5; i++) {
            char fmt[16];
            snprintf(fmt, sizeof(fmt), "%s=%%d", stereoDepthKeys[i]);
            if (sscanf(line, fmt, &v) == 1)
                tDepth[i] = v < -8 ? -8 : (v > 8 ? 8 : v);
        }
        if (sscanf(line, "FADE=%d", &v) == 1)
            *tFade = v < 0 ? 0 : (v > 8 ? 8 : v);
        if (sscanf(line, "HAZE=%d", &v) == 1)
            *tHaze = v < 0 ? 0 : (v > 8 ? 8 : v);
        if (sscanf(line, "BLUR=%d", &v) == 1)
            *tBlur = v < 0 ? 0 : (v > 8 ? 8 : v);
        if (sscanf(line, "FOCUSBACK=%d", &v) == 1)
            *tFB = v < -8 ? -8 : (v > 0 ? 0 : v);
        if (sscanf(line, "FOCUSFRONT=%d", &v) == 1)
            *tFF = v < 0 ? 0 : (v > 8 ? 8 : v);
        if (sscanf(line, "EDGEMODE=%d", &v) == 1)
            *tEdge = v < 0 ? 0 : (v > 2 ? 2 : v);
    }
    fclose(f);
}

static void settingsWriteStereo3DGlobals(FILE *f)
{
    fprintf(f, "# snes9x_3ds stereoscopic 3D depths (-8..8): + pops out, - sinks in\n");
    for (int i = 0; i < 5; i++)
        fprintf(f, "%s=%d\n", stereoDepthKeys[i], settings3DS.StereoDepth[i]);
    fprintf(f, "# effects apply only outside the focus zone [FOCUSBACK..FOCUSFRONT],\n");
    fprintf(f, "# growing linearly with the distance to the zone edge (0..8 each):\n");
    fprintf(f, "# fade darkens / haze fogs behind it, blur smudges in both directions\n");
    fprintf(f, "FADE=%d\n", settings3DS.StereoFade);
    fprintf(f, "HAZE=%d\n", settings3DS.StereoHaze);
    fprintf(f, "BLUR=%d\n", settings3DS.StereoBlur);
    fprintf(f, "FOCUSBACK=%d\n", settings3DS.StereoFocusBack);
    fprintf(f, "FOCUSFRONT=%d\n", settings3DS.StereoFocusFront);
    fprintf(f, "# edge cleanup for parallax side columns: 0 off, 1 trim, 2 zoom\n");
    fprintf(f, "EDGEMODE=%d\n", settings3DS.StereoEdgeMode);
}

// Writes the current game's LOOK (depths/focus/effects/edge) as the global
// default for games without their own .3d. Profiles/binds stay per-game.
void settingsSaveStereo3DDefault()
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/stereo3d/default.3d", settings3DS.RootDir);

    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# global default look - applied to games without their own .3d\n");
    settingsWriteStereo3DGlobals(f);
    fclose(f);
}

void settingsSaveStereo3D()
{
    char path[PATH_MAX];
    file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".3d", "stereo3d");
    if (path[0] == '\0') return;

    FILE *f = fopen(path, "w");
    if (!f) return;

    settingsWriteStereo3DGlobals(f);

    // per-scene profiles + PPU-signature binds (issue #23). BIND bytes:
    // b0=2105 b1=TM b2=TS b3=2130 b4=2131 b5=2106 b6=420C; mask byte 00
    // ignores that register.
    for (int pi = 0; pi < settings3DS.StereoProfilesCount; pi++) {
        const S9xSettings3DS::SStereoProfile *p = &settings3DS.StereoProfiles[pi];
        fprintf(f, "PROFILE=%s\n", p->Name);
        for (int i = 0; i < 5; i++)
            fprintf(f, "%s=%d\n", stereoDepthKeys[i], p->Depth[i]);
        fprintf(f, "FADE=%d\nHAZE=%d\nBLUR=%d\n", p->Fade, p->Haze, p->Blur);
        fprintf(f, "FOCUSBACK=%d\nFOCUSFRONT=%d\nEDGEMODE=%d\n",
            p->FocusBack, p->FocusFront, p->EdgeMode);
    }
    if (settings3DS.StereoWatchAddr >= 0)
        fprintf(f, "WATCH=%X\n", settings3DS.StereoWatchAddr);
    for (int bi = 0; bi < settings3DS.StereoBindsCount; bi++) {
        const S9xSettings3DS::SStereoBind *b = &settings3DS.StereoBinds[bi];
        fprintf(f, "BIND=%s:%016llX:%016llX:%016llX:%016llX:%02X\n",
            settings3DS.StereoProfiles[b->ProfileIdx].Name,
            (unsigned long long)b->Sig, (unsigned long long)b->Mask,
            (unsigned long long)b->Sig2, (unsigned long long)b->Mask2,
            b->WatchVal < 0 ? 0xFFFF : b->WatchVal);
    }
    fclose(f);
}

//----------------------------------------------------------------------
// Save settings by game.
//----------------------------------------------------------------------
bool settingsSave(bool includeGameSettings)
{
    if (!settings3DS.isDirty) return true;

    TickCounter timer;

    osTickCounterStart(&timer);

    cfgFileAvailable[0] = settingsReadWriteFullListGlobal(true);
    if (cfgFileAvailable[0]) {
        osTickCounterUpdate(&timer);
        log3dsWrite("Global settings saved in %.3fms", osTickCounterRead(&timer));
    }

    if (includeGameSettings) {
        osTickCounterStart(&timer);

        cfgFileAvailable[1] = settingsReadWriteFullListByGame(true);
        if (cfgFileAvailable[1]) {
            osTickCounterUpdate(&timer);
            log3dsWrite("Game settings saved in %.3fms", osTickCounterRead(&timer));
        }

        settingsSaveStereo3D();
    }
    
    settings3DS.isDirty = false;

    return true;
}

//-------------------------------------------------------
// Load the ROM and reset the CPU.
//-------------------------------------------------------
bool emulatorLoadRom()
{
    if (settings3DS.isRomLoaded) {
        // save previous game state
        impl3dsSaveStateAuto();
        impl3dsSaveCheats();
    }
    
    // save global + previous game config
    settingsSave(settings3DS.isRomLoaded);

    char romFileNameFullPath[PATH_MAX];
    snprintf(romFileNameFullPath, sizeof(romFileNameFullPath), "%s%s", file3dsGetCurrentDir(), romFileName);

    // Block the audio mixing thread from touching APU/memory state while
    // Memory.LoadROM tears down and rebuilds SNES9x globals AND while
    // dependent state is rebuilt (settings, slot state, savestate auto-load).
    // Without this the mixing thread faults reading half-initialised state.
    snd3dsDrainMixing();

    // when impl3dsLoadROM fails, our previous game (if any) is also unusable
    // therefore we always set ROMCRC32 to 0
    Memory.ROMCRC32 = 0;
    msu3dsOnEvent(Msu1Event::RomUnload);
    rewind3dsReset();
    settings3DS.isRomLoaded = impl3dsLoadROM(romFileNameFullPath) && Memory.ROMCRC32;

    if (!settings3DS.isRomLoaded) {
        snd3dsResumeMixing();
        return false;
    }

    log3dsWrite("ROM loaded: %s [%s] CRC=%08X %s %s %s %s %s",
                Memory.ROMName, Memory.ROMId, Memory.ROMCRC32,
                Memory.MapType(), Memory.Size(),
                (Memory.ROMSpeed & 0x10) ? "FastROM" : "SlowROM",
                (Memory.ROMRegion > 12 || Memory.ROMRegion < 2) ? "NTSC" : "PAL",
                Memory.KartContents());

    // clear stale data
    gpu3dsClearTexture(&GPU3DS.textures[SNES_MAIN], 0);
    gpu3dsClearTexture(&GPU3DS.textures[SNES_SUB], 0);
    gpu3dsClearTexture(&GPU3DS.textures[SNES_DEPTH], 0);

    // update global config
    snprintf(settings3DS.lastSelectedDir, sizeof(settings3DS.lastSelectedDir), "%s", file3dsGetCurrentDir());
    snprintf(settings3DS.lastSelectedFilename, sizeof(settings3DS.lastSelectedFilename), "%s", romFileName);

    settings3DS.isDirty = true;
    settings3dsResetGameDefaults();

    // if file exists, overwrite the defaults
    // if not, stay on defaults
    cfgFileAvailable[1] = settingsReadWriteFullListByGame(false);

    settingsLoadStereo3D();

    settings3dsUpdate(true);

    // MSU-1 games raise the mixer's syscore budget (FLAC decode); everyone
    // else keeps the small share - see SND3DS_O3DS_CPU_LIMIT (issue #54).
    snd3dsApplyCpuLimit();

    // reset hotkeys that conflict with the active circle pad binding
    bool cpadBound = settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalBindCirclePad : settings3DS.BindCirclePad;
    for (int i = 0; i < HOTKEYS_COUNT; ++i)
        ResetHotkeyIfNecessary(i, cpadBound);

    // cache which save slots already have a savestate for the loaded game
    for (int slot = 1; slot <= SAVESLOTS_MAX; ++slot)
        impl3dsUpdateSlotState(slot);

    if (settings3DS.SaveStateScreenshots) {
        impl3dsEnsureStateScreenshotDir();
    } else {
        impl3dsDeleteStateScreenshots();
    }

    if (settings3DS.AutoSavestate)
        impl3dsLoadStateAuto();

    float targetFps = (float)TICKS_PER_SEC / settings3DS.TicksPerFrame;
        notif3dsFpsUpdate(targetFps, settings3DS.GameScreen);

    snd3dsResumeMixing();
    return true;
}

//----------------------------------------------------------------------
// Find the ID of the last selected item in the file list.
//----------------------------------------------------------------------
int findLastSelected(std::vector<DirectoryEntry>& entries, const char* name)
{
    if (name == NULL || name[0] == '\0') {
		return -1;
	}

    for (size_t i = 0; i < entries.size(); i++)
    {
       if (strncmp(entries[i].Filename, name, sizeof(entries[i].Filename) - 1) == 0)
            return static_cast<int>(i);
    }

    return -1;
}

//----------------------------------------------------------------------
// Handle menu cheats.
//----------------------------------------------------------------------

bool syncCheatsFromMenu(std::vector<SMenuItem>& cheatMenu, bool applyCheats)
{
    if (cheatMenu.empty() || Cheat.num_cheats == 0) return false;

    bool cheatsUpdated = false;
    int cheatIndex = 0;

    for (const auto& item : cheatMenu) {
        if (cheatIndex >= MAX_CHEATS || static_cast<uint32>(cheatIndex) >= Cheat.num_cheats) {
            break;
        }

        if (item.Type != MenuItemType::Checkbox) {
            continue;
        }
        
        bool enabledInMenu = (item.Value == 1);
                
        if (Cheat.c[cheatIndex].enabled != enabledInMenu) {
            Cheat.c[cheatIndex].enabled = enabledInMenu;
            
            if (applyCheats) {
                if (enabledInMenu) 
                    S9xEnableCheat(cheatIndex);
                else 
                    S9xDisableCheat(cheatIndex);
            }

            cheatsUpdated = true;
        }
        
        cheatIndex++;
    }

    return cheatsUpdated;
}

// returns the index of the item matching 'selectedItemName', or 0 if not found/empty
int fillFileMenuEntries(std::vector<SMenuItem>& fileMenu, const char *selectedItemName) {
    fileMenu.clear();
    fileMenu.reserve(entries.size());

    int selectedItemIndex = 0;

    for (size_t i = 0; i < entries.size(); ++i) {
        // get the permanent address of the item in the global vector
        const DirectoryEntry* entry = &entries[i];

        const char* prefix = MENU_PREFIX_FILE; 

        if (entry->Type == FileEntryType::ChildDirectory)
            prefix = MENU_PREFIX_CHILD_DIRECTORY;
        else if (entry->Type == FileEntryType::ParentDirectory)
            prefix = MENU_PREFIX_PARENT_DIRECTORY;

        // MSU pack folders are shown by their folder name, booting the ROM inside
        char displayName[NAME_MAX + 1];
        if (entry->Type == FileEntryType::VirtualFile) {
            file3dsGetVirtualDisplayName(*entry, displayName, sizeof(displayName));
        } else {
            snprintf(displayName, sizeof(displayName), "%s", entry->Filename);
        }

        char label[NAME_MAX + 1];
        const size_t prefixLen = strlen(prefix);
        const size_t maxFilenameLen = (prefixLen < sizeof(label) - 1) ? (sizeof(label) - 1 - prefixLen) : 0;
        snprintf(label, sizeof(label), "%s%.*s", prefix, (int)maxFilenameLen, displayName);

        if (selectedItemName && selectedItemName[0] != '\0') {
            if (strncmp(entry->Filename, selectedItemName, NAME_MAX) == 0) {
                selectedItemIndex = i;
            }
        }

        fileMenu.emplace_back(
            // Do NOT use [&entry] here (previous implementation, dangling reference)
            // capture the pointer by value [entry]
            [entry](int val) { 
                selectedEntry = entry; 
            }, 
            MenuItemType::Action, 
            label, 
            "", 
            99999
        );

        // MSU pack entries stand out in blue (issue #29);
        // under the cursor they go light blue instead of the theme color
        if (entry->Type == FileEntryType::VirtualFile) {
            fileMenu.back().TextColor = 0x529eeb;
            fileMenu.back().SelectedTextColor = 0x9ccdff;
        }
    }

    return selectedItemIndex;
}


// firstItemIndex < 0 keeps the current scroll offset (rescan/delete/initial build);
// >= 0 forces it (0 = top when entering a child, or a restored offset going back up).
void updateFileMenuTab(const char *selectedItemName, bool showCachingIndicator, int firstItemIndex = -1) {
    SMenuTab& fileMenuTab = menuTabs.back();

    fileMenuTab.SubTitle.assign(file3dsGetCurrentDir());

    file3dsGetFiles(entries, menuTabs, showCachingIndicator);
    fileMenuTab.SelectedItemIndex = fillFileMenuEntries(fileMenuTab.MenuItems, selectedItemName);
    if (firstItemIndex >= 0) {
        fileMenuTab.FirstItemIndex = firstItemIndex;
    }
    fileMenuTab.MakeSureSelectionIsOnScreen(MENU_HEIGHT, 2);
}

// Applies a background directory-cache refresh (issue #6) on the UI thread:
// swaps in the fresh list and rebuilds the file tab, keeping the selection.
static void fileMenuIdleTick() {
    if (menuTabs.empty()) return;

    SMenuTab& fileMenuTab = menuTabs.back();

    char selectedName[NAME_MAX + 1] = "";
    int selected = fileMenuTab.SelectedItemIndex;
    if (selected >= 0 && selected < static_cast<int>(entries.size())) {
        snprintf(selectedName, sizeof(selectedName), "%s", entries[selected].Filename);
    }

    if (!file3dsBgRefreshTake(entries)) return;

    fileMenuTab.SelectedItemIndex = fillFileMenuEntries(fileMenuTab.MenuItems, selectedName);
    fileMenuTab.MakeSureSelectionIsOnScreen(MENU_HEIGHT, 2);
    menu3dsSetScreenDirty(true, true);
}

void setupMenu(int& currentMenuTab) {
    menu3dsSetIdleCallback(fileMenuIdleTick);
    int requiredTabs = settings3DS.isRomLoaded ? 5 : 2;
    int fileMenuTabIndex = settings3DS.isRomLoaded ? 4 : 1;
    bool isFirstRun = menuTabs.empty();
    bool requiredTabsChanged = menuTabs.size() != static_cast<size_t>(requiredTabs);
    bool romChanged = !isFirstRun && Memory.ROMCRC32 != lastLoadedRomCRC;
    bool preserveSelection = !requiredTabsChanged && !romChanged;

    // only reallocate if the size grows, otherwise reuse the buffer
    if (requiredTabsChanged) {
        menuTabs.resize(requiredTabs);
    }

    const char* tabsStart[] = { "Emulator", "Load Game" };
    const char* tabsGame[] = { "Emulator", "Settings", "Controls", "Cheats", "Load Game" };
    const char** tabs = settings3DS.isRomLoaded ? tabsGame : tabsStart;

    for (int i = 0; i < requiredTabs; i++) {
        if (i != fileMenuTabIndex) {
            // skip clean tabs
            if (!(requiredTabsChanged || romChanged) && !settings3DS.menuTabDirty[i])
                continue;

            menuTabs[i].SetTitle(tabs[i]);
            menuTabs[i].SubTitle.clear();

            switch (i) {
                case 0:
                    makeEmulatorMenu(menuTabs[i].MenuItems, menuTabs, currentMenuTab);
                    break;
                case 1:
                    makeOptionMenu(menuTabs[i].MenuItems, menuTabs, currentMenuTab);
                    break;
                case 2:
                    makeControlsMenu(menuTabs[i].MenuItems, menuTabs, currentMenuTab);
                    break;
                case 3:
                    makeCheatMenu(menuTabs[i].MenuItems);
                    break;
            }

            int selected = menuTabs[i].SelectedItemIndex;
            bool validSelection = preserveSelection
                && selected >= 0 && selected < static_cast<int>(menuTabs[i].MenuItems.size())
                && menuTabs[i].MenuItems[selected].IsHighlightable();

            if (!validSelection) {
                for (size_t j = 0; j < menuTabs[i].MenuItems.size(); j++) {
                    if (menuTabs[i].MenuItems[j].IsHighlightable()) {
                        menuTabs[i].SelectedItemIndex = static_cast<int>(j);
                        break;
                    }
                }
            }

            menuTabs[i].MakeSureSelectionIsOnScreen(MENU_HEIGHT, 2);
        } else {
            // file tab is expensive and content is layout-/navigation-driven, not ROM-driven
            if (!requiredTabsChanged)
                continue;

            menuTabs[i].SetTitle(tabs[i]);
            menuTabs[i].SubTitle.clear();
            updateFileMenuTab(settings3DS.lastSelectedFilename, !isFirstRun);
        }
    }

    for (int i = 0; i < TAB_DIRTY_COUNT; i++)
        settings3DS.menuTabDirty[i] = false;
}

FileMenuOption showFileMenuOptions(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab) {
    int selectedItemIndex = menuTabs[currentMenuTab].SelectedItemIndex;
    DirectoryEntry *entry = &entries[selectedItemIndex];

    char selectedFileName[NAME_MAX + 1];
    
    if (entry->Type == FileEntryType::File) {
        snprintf(selectedFileName, sizeof(selectedFileName), "%s", entry->Filename);
    } else {
        selectedFileName[0] = '\0';
    }

    std::vector<FileMenuOption> options;
    std::vector<SMenuItem> menuItems = makeOptionsForFileMenu(options, selectedFileName);
    
    int optionIndex = menu3dsShowDialog(
        dialogTab, isDialog, currentMenuTab, menuTabs, 
        "File Menu Options", 
        "On startup, the default directory is shown.\nIf unset, it resumes from the last played game's location. If ROMs changed, refresh the ROM list.", 
        Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, 
        menuItems
    );

    if (optionIndex < 0 || optionIndex >= static_cast<int>(options.size())) {
        if (isDialog) {
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        }
        return FileMenuOption::None;
    }

    FileMenuOption option = options[optionIndex];

    switch (option) {
        case FileMenuOption::SetDefaultDir:
            file3dsSetDefaultDir(false);
            break;

        case FileMenuOption::ResetDefaultDir:
            file3dsSetDefaultDir(true);
            break;

        case FileMenuOption::RandomGame:
        {
            // implies directories first, then ROMs
            int minIndex = entries.size() - file3dsGetCurrentDirRomCount();
            int maxIndex = entries.size() - 1;

            // we could exclude lastSelectedFilename here, but let's keep it simple for now
            menu3dsSelectRandomGameIndex(menuTabs[currentMenuTab], minIndex, maxIndex, -1);

            break;
        }
            
        case FileMenuOption::RescanDir:
        {
            dialogTab.Title = "Rescanning directory...";
            dialogTab.DialogText = "";
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs);
            menu3dsSwapBuffersAndWaitForVBlank();

            file3dsDeleteCurrentDirCache();
            updateFileMenuTab(NULL, false);

            char cachePath[PATH_MAX];
            char message[PATH_MAX + 50];
            file3dsGetCurrentDirCacheName(cachePath, sizeof(cachePath));
            snprintf(message, sizeof(message), "Directory cache created (%s).", cachePath);
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Success", message, Themes[static_cast<int>(settings3DS.Theme)].dialogColorSuccess, makeOptionsForOk(), -1, false);
            
            break;
        }

        case FileMenuOption::DeleteGame:
        {
            char message[NAME_MAX + 64];
            snprintf(message, sizeof(message), "Do you really want to remove \"%s\" from your SD card?", selectedFileName);
            bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Delete Game", message, false, false);

            if (confirmed) {
                // if current selected game is also last selected game, reset lastSelectedFilename
                if (strcmp(selectedFileName, settings3DS.lastSelectedFilename) == 0) {
                    settings3DS.lastSelectedFilename[0] = '\0';
                    settings3DS.isDirty = true;
                }

                char path[PATH_MAX];
                snprintf(path, sizeof(path), "%s%s", file3dsGetCurrentDir(), selectedFileName);
                dialogTab.Title = "Deleting...";
                dialogTab.DialogText = "";
                menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs);
                menu3dsSwapBuffersAndWaitForVBlank();
                
                if (remove(path) == 0) {
                    file3dsDeleteCurrentDirCache();

                    char nextSelectedFilename[NAME_MAX + 1] = ""; 
                    bool listIsEmpty = (entries.size() <= 1);
                    bool isLastItem = listIsEmpty || (selectedItemIndex >= static_cast<int>(entries.size()) - 1);

                    if (!listIsEmpty) {
                        // grab the next filename before we clear the vector
                        int nextIndex = isLastItem ? selectedItemIndex - 1 : selectedItemIndex + 1;
                        if (nextIndex >= 0 && nextIndex < static_cast<int>(entries.size())) {
                            snprintf(nextSelectedFilename, sizeof(nextSelectedFilename), "%s", entries[nextIndex].Filename);
                        }
                    }

                    updateFileMenuTab(listIsEmpty ? NULL : nextSelectedFilename, false);

                    snprintf(message, sizeof(message), "%s removed from SD card.", selectedFileName);
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Success", message, Themes[static_cast<int>(settings3DS.Theme)].dialogColorSuccess, makeOptionsForOk(), -1, false);                
                } else {
                    snprintf(message, sizeof(message), "Failed to remove %s", selectedFileName);
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Error", message, Themes[static_cast<int>(settings3DS.Theme)].dialogColorWarn, makeOptionsForOk(), -1, false);
                }
            }
            break;
        }
        default: 
            // none selected
            break;
    }

    if (isDialog) {
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs, option != FileMenuOption::RandomGame);
    }

    return option;
}

void onDirectoryEntrySelected(
    SMenuTab& dialogTab, 
    bool& isDialog, 
    int currentMenuTab, 
    bool& runNextGame,
    std::vector<SMenuItem>& cheatMenu,
    const DirectoryEntry*& entry
) {
    if (entry->Type == FileEntryType::File || entry->Type == FileEntryType::VirtualFile) 
    {
        snprintf(romFileName, sizeof(romFileName), "%s", entry->Filename);

        char basename[NAME_MAX + 1];
        utils3dsGetBasename(romFileName, basename, sizeof(basename), false);
        menu3dsShowRomLoadingDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Loading Game:", basename, Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo, romFileName);
        
        if (syncCheatsFromMenu(cheatMenu, false)) {
            settings3DS.cheatsDirty = true;
        }
        
        runNextGame = emulatorLoadRom();
        
        if (!runNextGame) {
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Loading Game:", "Oops. Unable to load Game", Themes[static_cast<int>(settings3DS.Theme)].dialogColorWarn, makeOptionsForOk(), -1, false);
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        } else {
            GPU3DS.emulatorState = EMUSTATE_EMULATE;
        }
    } 
    else if (entry->Type == FileEntryType::ParentDirectory || entry->Type == FileEntryType::ChildDirectory) 
    {
        char lastDirectoryName[128] = ""; // e.g. for "sdmc:/roms/snes/EUR" it's "EUR"
        int restoreFirstItemIndex;

        if (entry->Type == FileEntryType::ParentDirectory) {
            file3dsGetCurrentDirName(lastDirectoryName, sizeof(lastDirectoryName));
            restoreFirstItemIndex = fileMenuScrollStack.empty() ? 0 : fileMenuScrollStack.back();
            if (!fileMenuScrollStack.empty()) {
                fileMenuScrollStack.pop_back();
            }
        } else {
            fileMenuScrollStack.push_back(menuTabs.back().FirstItemIndex);
            restoreFirstItemIndex = 0; // start a fresh child directory at the top
        }

        file3dsGoUpOrDownDirectory(*entry);
        updateFileMenuTab(lastDirectoryName, true, restoreFirstItemIndex);
    }
}

// Rewind timeline (issue #42): the Yes/No confirmation is the menu's own
// modal dialog, run from the game context. The tabs may not exist yet -
// entering via hotkey on an autobooted game means showMenu() never built
// them, and the dialog's backdrop draws menuTabs[currentMenuTab] (this
// empty-vector access is what crashed the first attempt at reusing it).
bool rewind3dsConfirmResume()
{
    int currentMenuTab = menu3dsGetLastSelectedTabIndex();
    if (menuTabs.empty() || Memory.ROMCRC32 != lastLoadedRomCRC || menu3dsHasDirtyTabs())
    {
        setupMenu(currentMenuTab);
        lastLoadedRomCRC = Memory.ROMCRC32;
    }

    SMenuTab dialogTab;
    bool isDialog = false;
    // hideAfter=false: the hide animation draws the real MENU behind the
    // fading dialog - the blink to the main menu on Yes (field report
    // 16/08). The timeline repaints the whole screen right after either
    // answer, so the dialog can simply vanish under it.
    return confirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
        "Rewind", "Resume the game from this moment?", true, false);
}

void showMenu() {
    static std::vector<SMenuItem> emptyCheats;
    int currentMenuTab = menu3dsGetLastSelectedTabIndex();

    // opening the menu points 'Editing Profile' at the profile the current
    // screen matches, not whichever one was edited last
    if (settings3DS.isRomLoaded) {
        int activeIdx = settings3dsStereoActiveIndex();
        if (activeIdx != s_stereoEditIdx) {
            s_stereoEditIdx = activeIdx;
            menu3dsMarkTabDirty(TAB_SETTINGS);
        }
    }

    // 1. first boot
    // 2. new game loaded
    if (menuTabs.empty() || Memory.ROMCRC32 != lastLoadedRomCRC || menu3dsHasDirtyTabs())
    {
        setupMenu(currentMenuTab);
        lastLoadedRomCRC = Memory.ROMCRC32;
    }

    std::vector<SMenuItem>& cheatMenu = settings3DS.isRomLoaded ? menuTabs[3].MenuItems : emptyCheats;

    bool isDialog = false;
    bool runNextGame = false;
    SMenuTab dialogTab;

    if (g_menuProbeActive) {
        g_menuProbeActive = false;
        g_menuProbeCountdown = 600;          // re-arm the next round-trip
        GPU3DS.emulatorState = EMUSTATE_EMULATE;
        log3dsWrite("[menuprobe] leaving menu (instant)");
    }

    while (aptMainLoop() && GPU3DS.emulatorState == EMUSTATE_PAUSEMENU) {
        int result = menu3dsMenuSelectItem(dialogTab, isDialog, currentMenuTab, menuTabs);

        if (menu3dsHasDirtyTabs())
        {
            setupMenu(currentMenuTab);
        }

        // user pressed X button in file menu
        // selectedEntry is set for option FileMenuOption::RandomGame
        if (result == MENU_ENTRY_CONTEXT_MENU) 
        {
            showFileMenuOptions(dialogTab, isDialog, currentMenuTab);
        }

        // user pressed START button in pause menu -> continue game
        if (result == MENU_CONTINUE_GAME) 
        {
            GPU3DS.emulatorState = EMUSTATE_EMULATE;
        }
        else if (selectedEntry) 
        {
            onDirectoryEntrySelected(dialogTab, isDialog, currentMenuTab, runNextGame, cheatMenu, selectedEntry);
            selectedEntry = nullptr;
        }
    }
    
    // load/resume game
    if (GPU3DS.emulatorState == EMUSTATE_EMULATE) {
        input3dsWaitForRelease();

        if (!runNextGame) {
            if (syncCheatsFromMenu(cheatMenu, true)) {
                settings3DS.cheatsDirty = true;
            }
        
            settings3dsUpdate(true);
        }

        // point to first tab when running a new game
        menu3dsSetLastSelectedTabIndex(runNextGame ? 0 : currentMenuTab);

        impl3dsUpdateUiAssets();

        if (slotLoaded) {
            if (impl3dsHasBrokenAudioStateSignature()) {
                char path[PATH_MAX], ext[16];
                snprintf(ext, sizeof(ext), ".%d.frz", settings3DS.CurrentSaveSlot);
                file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ext, "savestates");
                impl3dsLogBrokenAudioSignatureContext("load-menu", path);
                notif3dsTrigger(Notif::BrokenAudioLoad, Notif::Type::Warning, settings3DS.GameScreen);
            } else {
                notif3dsTrigger(Notif::LoadState, Notif::Type::Success, settings3DS.GameScreen);
            }

            slotLoaded = false;
        }
    }

    menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTabs);
    menu3dsSetScreenDirty();
}

//--------------------------------------------------------
// Initialize the emulator engine and everything else.
// This calls the impl3dsInitialize, which executes
// initialization code specific to the emulation core.
//--------------------------------------------------------
bool emulatorInitialize()
{
    log3dsInitialize();
    log3dsWrite("==== START Logging (%s, %s) ====", settings3dsGetAppVersion("v"), log3dsGetCurrentDate());

    GSPGPU_FramebufferFormat gpuBufFmt = (GSPGPU_FramebufferFormat)DISPLAY_TRANSFER_FMT;
    gfxInit(gpuBufFmt, gpuBufFmt, false);
    // draw our start up message as early as possible
    ui3dsPrepare();
    menu3dsShowSplashMessage("Loading");
    
    Result rc = romfsInit();
    settings3DS.isRomFsLoaded = !R_FAILED(rc);
    
    if (settings3DS.isRomFsLoaded) {
        file3dsSetRomNameMappings("romfs:/mappings.txt");
    }

    if (!file3dsInitialize()) return false;
    if (!gpu3dsInitialize()) return false;
    if (!ui3dsInitialize()) return false;
    if (!notif3dsInitialize()) return false;
    if (!impl3dsInitialize()) return false;
    if (!img3dsInitialize()) return false;
    if (!snd3dsInitialize()) return false;

    // Fence MSU-1 register writes against the mixing thread. Unconditional:
    // snesAccessLock exists even when NDSP init failed (audioType != 2).
    msu1_set_lock_hooks(snd3dsLockSnesAccess, snd3dsUnlockSnesAccess);

    if (snd3DS.audioType == 2) msu3dsNdspInstall();

    enableAptHooks();

    #ifndef PROFILING_DISABLED
        t3dsResetTimers();
    #endif

	log3dsWrite("#### memory in use ####");
    log3dsWrite("linear: %dkb / %dkb", (GPU3DS.linearMemTotal - linearSpaceFree()) / 1024, GPU3DS.linearMemTotal / 1024);
    log3dsWrite("vram: %dkb / %dkb", (GPU3DS.vramTotal - vramSpaceFree()) / 1024, GPU3DS.vramTotal / 1024);
	log3dsWrite("---- initialized ----");

    return true;
}

//--------------------------------------------------------
// Finalize the emulator.
//--------------------------------------------------------
int emulatorFinalize()
{
	log3dsWrite("---- emulatorFinalize ----");
    consoleClear();
    disableAptHooks();

    // Park the mixer before tearing down channel 1: under drain the fill
    // queues nothing, so AppExit's clear_queue/shutdown cannot race it.
    // Guarded: the mixing thread (and the initialized lock) only exist
    // when NDSP came up.
    if (snd3DS.audioType == 2) snd3dsDrainMixing();

    msu3dsOnEvent(Msu1Event::AppExit);
    msu3dsNdspUninstall();

    snd3dsFinalize();
    impl3dsFinalize();
    img3dsFinalize();
    ui3dsFinalize();
    gpu3dsFinalize();
    file3dsFinalize();
    romfsExit();

    osSetSpeedupEnable(false);

    log3dsWrite("==== END Logging (%s, %s) ====", settings3dsGetAppVersion("v"), log3dsGetCurrentDate());
    log3dsClose();

    return 0;
}


//---------------------------------------------------------
// decides whether to sleep, skip rendering,
// or accept slowdown based on accumulated skew.
//---------------------------------------------------------
bool paceFrame(long actualTicksThisFrame, int totalFrames, long &snesFrameTotalActualTicks, long &snesFrameTotalAccurateTicks, int &snesFramesSkipped)
{
    snesFrameTotalActualTicks += actualTicksThisFrame;
    snesFrameTotalAccurateTicks += settings3DS.TicksPerFrame;
    long skew = snesFrameTotalAccurateTicks - snesFrameTotalActualTicks;

    if (skew < 0)
    {
        // Running slow. Skip rendering if beyond 10% of a frame
        // and we haven't hit the max skip limit yet.
        if (skew < -settings3DS.TicksPerFrame / 10 && snesFramesSkipped < settings3DS.MaxFrameSkips)
        {
            snesFramesSkipped++;
            return true;  // skip next frame's rendering
        }

        // skipping didn't help — accept slowdown, reset window
        if (snesFramesSkipped >= settings3DS.MaxFrameSkips)
        {
            snesFramesSkipped = 0;
            snesFrameTotalActualTicks = actualTicksThisFrame;
            snesFrameTotalAccurateTicks = settings3DS.TicksPerFrame;
        }

        return false;
    }

    // On pace or ahead — reset timing window
    snesFrameTotalActualTicks = 0;
    snesFrameTotalAccurateTicks = 0;
    snesFramesSkipped = 0;

    if (settings3DS.TurboMode)
        return (totalFrames % 2) == 0;

    if (settings3DS.FrameSync == Setting::FrameSync::Sleep || !GPU3DS.isReal3DS)
        svcSleepThread((s64)((double)skew * 1e9 / TICKS_PER_SEC));
    else
        gpu3dsWaitForVBlank(settings3DS.GameScreen);

    return false;
}

//---------------------------------------------------------
// Prints profiling timer data to the second screen.
//---------------------------------------------------------

void updateProfilingOutput(int totalFrames)
{
    #ifndef PROFILING_DISABLED
        if (GPU3DS.profilingMode == PROFILING_OFF)
            return;

        if (totalFrames % PROFILING_WINDOW_FRAMES == 0) {
            t3dsPrintTimers(PROFILING_WINDOW_FRAMES);
            t3dsResetTimers();
        }
    #endif
}

//----------------------------------------------------------
// This is the main emulation loop. It calls the 
//    impl3dsRunOneFrame
//   (which must be implemented for any new core)
// for the execution of the frame.
//----------------------------------------------------------
void emulatorLoop()
{
    // menu is currently rendered via software and may have configured the screen for
    // a lower color depth than our other screen content rendered via GPU.
    // therefore we check the screen format first to ensure pixel data is interpreted correctly
    GSPGPU_FramebufferFormat gpuBufFmt = (GSPGPU_FramebufferFormat)DISPLAY_TRANSFER_FMT;

    if (gfxGetScreenFormat(settings3DS.SecondScreen) != gpuBufFmt) {
        gfxSetScreenFormat(settings3DS.SecondScreen, gpuBufFmt);
    }

    gpu3dsResetState();

    GPU3DSExt.render2x.enabled = settings3DS.EnhancedResolution != Setting::EnhancedResolution::Off;

    // Menu-triggered timeline runs BEFORE any repaint: the menu image
    // stays on the second screen until the timeline draws over it - the
    // format switch and wallpaper below were the blink on the way in
    // (field report 16/08). B inside the timeline goes back to the menu,
    // and returning early skips the same repaints on the way out.
    if (rewind3dsTakeTimelineRequest()) {
        gpu3dsSetTopMode();
        rewind3dsTimelineShow();
        if (GPU3DS.emulatorState == EMUSTATE_PAUSEMENU)
            return;
    }

    // render one frame before lcd3dsSetEmulationRate below,
    // otherwise game screen glitches and real hardware freezes.
    if (gpu3dsSetTopMode()) {
        gpu3dsFrameBegin(C3D_FRAME_SYNCDRAW, false);
            impl3dsSceneRender(true);
        gpu3dsFrameEnd();
    }

    if (GPU3DS.profilingMode == PROFILING_OFF) {
        for (int pass = 0; pass < 2; pass++) {
            gpu3dsFrameBegin(C3D_FRAME_SYNCDRAW, false, true);
                gpu3dsClearScreen(settings3DS.SecondScreen);
                img3dsDrawBackground(UI_BG_SECOND);
            gpu3dsFrameEnd();
        }
    } else {
        // consoleInit(...) sets double buffering to false
        // make sure to enable double buffering again when leaving emulatorLoop()
        consoleInit(settings3DS.SecondScreen, NULL);
        t3dsResetTimers();
    }

    int totalFrames = 0;
    int fpsFrameCount = 0;

    int  snesFramesSkipped = 0;
    long snesFrameTotalActualTicks = 0;
    long snesFrameTotalAccurateTicks = 0;

    snd3dsResumeMixing();
    snd3dsStartPlaying();
    msu3dsOnEvent(Msu1Event::MenuExit);

    lcd3dsSetEmulationRate(settings3DS.TicksPerFrame);

    u64 frameCountTick = svcGetSystemTick();
    bool firstFrame = true;
    bool skipDrawing = true; // skip first ingame render to show game screen faster after menu exit
    SGPU_PROFILING_MODE lastProfilingMode = GPU3DS.profilingMode;

    while (aptMainLoop() && GPU3DS.emulatorState == EMUSTATE_EMULATE)
    {
        u64 startFrameTick = svcGetSystemTick();

        input3dsScanInputForEmulation();

        if (GPU3DS.profilingMode != lastProfilingMode) {
            if (lastProfilingMode == PROFILING_OFF) {
                gpu3dsClearScreen(settings3DS.SecondScreen);
                consoleInit(settings3DS.SecondScreen, NULL);
            }

            lastProfilingMode = GPU3DS.profilingMode;
            consoleClear();
            t3dsResetTimers();
        }

        updateProfilingOutput(++totalFrames);

        t3dsStartTimer(TIMER_RUN_ONE_FRAME);
        impl3dsRunOneFrame(firstFrame, skipDrawing);
        t3dsStopTimer(TIMER_RUN_ONE_FRAME);

        {
            // frame load feeds the graduated capture patience: a due
            // capture waits for an idle frame, relaxing its bar over time
            long ticksSoFar = (long)(svcGetSystemTick() - startFrameTick);
            int frameLoadPercent = (int)(ticksSoFar * 100 / (long)settings3DS.TicksPerFrame);
            rewind3dsFrameTick(input3dsIsRewindHoldPressed(), frameLoadPercent);
        }

        if (rewind3dsTakeHoldRequest()) {
            rewind3dsHoldShow();
            firstFrame = true;
            snesFrameTotalActualTicks = 0;
            snesFrameTotalAccurateTicks = 0;
            snesFramesSkipped = 0;
            startFrameTick = svcGetSystemTick();
        }

        if (rewind3dsTakeTimelineRequest()) {
            rewind3dsTimelineShow();
            firstFrame = true;   // repaint cleanly after the modal screen

            // The modal session's wall time sits inside this iteration's
            // startFrameTick window; left alone, paceFrame reads it as
            // seconds of schedule debt and skips drawing until "caught
            // up" - a frozen screen with audio running (field report
            // 16/08). Resume on a fresh schedule instead.
            snesFrameTotalActualTicks = 0;
            snesFrameTotalAccurateTicks = 0;
            snesFramesSkipped = 0;
            startFrameTick = svcGetSystemTick();
        }


        if (g_menuProbeArmed && g_menuProbeCountdown > 0 && --g_menuProbeCountdown == 0) {
            g_menuProbeActive = true;
            GPU3DS.emulatorState = EMUSTATE_PAUSEMENU;
            log3dsWrite("[menuprobe] entering menu");        }

        long actualTicksThisFrame = (long)(svcGetSystemTick() - startFrameTick);
        skipDrawing = paceFrame(actualTicksThisFrame, totalFrames, snesFrameTotalActualTicks, snesFrameTotalAccurateTicks, snesFramesSkipped);

        // FPS display (~every second)
        float targetFps = (float)TICKS_PER_SEC / settings3DS.TicksPerFrame;
        if (++fpsFrameCount >= (int)targetFps)
        {
            u64 now = svcGetSystemTick();

            if (skipNextFpsUpdate) {
                skipNextFpsUpdate = false;
                frameCountTick = now;
                fpsFrameCount = 0;
                firstFrame = false;
                continue;
            }

            float elapsed = (float)(now - frameCountTick) / TICKS_PER_SEC;
            float fps = fpsFrameCount / elapsed;

            notif3dsFpsUpdate(fps, settings3DS.GameScreen);
            frameCountTick = now;
            fpsFrameCount = 0;
        }

        firstFrame = false;
    }

    // underrun visibility for Old-3DS SD-latency diagnosis (docs/msu1.md checklist)
    if (Settings.MSU1)
        log3dsWrite("MSU-1 underruns: %u", (unsigned)msu3dsGetUnderrunCount());

    msu3dsOnEvent(Msu1Event::MenuEnter);
    snd3dsStopPlaying();
    snd3dsResumeMixing();
    lcd3dsRestoreDefaultRate();

    gfxSetDoubleBuffering(settings3DS.SecondScreen, true);
}

//---------------------------------------------------------
// Main entrypoint.
//---------------------------------------------------------
// Dev convenience (used for emulator-driven validation, e.g. Azahar): boot
// straight into the ROM whose absolute path is the first line of
// sdmc:/autoboot.txt. Absent/unreadable file = normal menu boot.
static void msu1LogToFile(const char* message) { log3dsWrite("[msu1] %s", message); }


static bool tryAutoBoot()
{
    FILE* f = fopen("sdmc:/autoboot.txt", "r");
    if (f == NULL) { return false; }
    settings3DS.LogFileEnabled = 1;   // autoboot is a dev/validation mode
    log3dsInitialize();
    FILE* probe = fopen("sdmc:/menuprobe.txt", "r");
    if (probe != NULL) {
        fclose(probe);
        g_menuProbeArmed = true;
        g_menuProbeCountdown = 600;
        log3dsWrite("[menuprobe] armed: instant menu round-trip every ~600 frames");
    }
    char path[PATH_MAX] = {};
    bool ok = fgets(path, sizeof(path), f) != NULL;
    fclose(f);
    if (!ok) { return false; }
    path[strcspn(path, "\r\n")] = '\0';
    char* slash = strrchr(path, '/');
    if (slash == NULL || slash[1] == '\0') { return false; }
    *slash = '\0';
    file3dsSetCurrentDir(path);
    snprintf(romFileName, sizeof(romFileName), "%s", slash + 1);
    log3dsWrite("[autoboot] %s/%s", path, romFileName);
    return emulatorLoadRom();
}

int main()
{
    APT_CheckNew3DS(&settings3DS.isNew3DS);
    osSetSpeedupEnable(true);
    utils3dsInitialize();

    // ---- load/update settings first ----
    menu3dsSetHotkeysData(hotkeysData);
    settings3dsResetGlobalDefaults();
    settings3dsResetGameDefaults();
    // load global config, overwrites defaults if file exists
    cfgFileAvailable[0] = settingsReadWriteFullListGlobal(false);
    settings3dsUpdate(false);

    // honor the 3DS Mode preference (no-op on Old 3DS/2DS)
    apply3dsMode(settings3DS.Overclock);

    if (!emulatorInitialize()) {
        return emulatorFinalize();
    }

    // msu1 diagnostics feed the session log whenever logging is enabled
    // (log3dsWrite is a no-op otherwise) — menu boots included, not just autoboot.
    msu1_set_log_hook(msu1LogToFile);
    
    img3dsSetThumbMode();
    gfxSetDoubleBuffering(settings3DS.SecondScreen, true);

    GPU3DS.emulatorState = tryAutoBoot() ? EMUSTATE_EMULATE : EMUSTATE_PAUSEMENU;
    
    while (aptMainLoop() && GPU3DS.emulatorState != EMUSTATE_END) {
        switch (GPU3DS.emulatorState) {
            case EMUSTATE_PAUSEMENU:
                showMenu();
                break;
            case EMUSTATE_EMULATE:
                emulatorLoop();
                break;
            default:
                GPU3DS.emulatorState = EMUSTATE_END;
        }
    }

    log3dsWrite("==== EXIT emulator ====");

    menu3dsShowSplashMessage("Saving & Exiting");
    
    settingsSave(settings3DS.isRomLoaded);
    impl3dsSaveStateAuto();
    impl3dsSaveCheats();

    // clear global vectors first
    entries.clear();    
    menuTabs.clear();

    return emulatorFinalize();
}
