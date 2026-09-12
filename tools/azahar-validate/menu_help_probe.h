// Test-only deterministic menu navigation; never enabled in release builds.
struct MenuHelpProbeCase {
    int tab;
    const char* label;
};
static const MenuHelpProbeCase MENU_HELP_PROBE_CASES[] = {
    {TAB_EMULATOR, "  Auto Save / Auto Load"},
    {TAB_EMULATOR, "  Create screenshot when saving"},
    {TAB_EMULATOR, "  Enable Logging (use when issues occur)"},
    {TAB_SETTINGS, "  Automatically save state on exit, load state on start"},
    {TAB_SETTINGS, "  Force SRAM Write on Pause"},
    {TAB_SETTINGS, "  BG1"},
    {TAB_SETTINGS, "  BG2"},
    {TAB_SETTINGS, "  BG3"},
    {TAB_CONTROLS, "  Bind Circle Pad to D-Pad"}
};
static size_t menu_help_probe_case = 0;

static bool menu_help_probe_save(const char* path)
{
    if (path == nullptr || settings3DS.SecondScreenWidth != 320 ||
        gfxGetScreenFormat(settings3DS.SecondScreen) != GSP_RGB565_OES) {
        return false;
    }
    const u16* fb = reinterpret_cast<const u16*>(gfxGetFramebuffer(settings3DS.SecondScreen, GFX_LEFT, nullptr, nullptr));
    if (fb == nullptr) {
        return false;
    }
    FILE* file = fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    static const char HEADER[] = "P6\n320 240\n255\n";
    bool ok = fwrite(HEADER, 1, sizeof(HEADER) - 1, file) == sizeof(HEADER) - 1;
    u8 row[320 * 3];
    for (int y = 0; ok && y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            const u16 pixel = fb[x * 240 + 239 - y];
            row[x * 3] = ((pixel >> 11) & 31) * 255 / 31;
            row[x * 3 + 1] = ((pixel >> 5) & 63) * 255 / 63;
            row[x * 3 + 2] = (pixel & 31) * 255 / 31;
        }
        ok = fwrite(row, 1, sizeof(row), file) == sizeof(row);
    }
    return fclose(file) == 0 && ok;
}

#ifdef PROBE_LAYER_TOGGLE
#include "layer_toggle_probe.h"
#endif

static void menu_help_probe_tick(int& frames, bool is_dialog, int& tab_index,
                                std::vector<SMenuTab>& tabs, SMenuTab*& current,
                                u32& keys_down)
{
#ifdef PROBE_LAYER_TOGGLE
    layer_toggle_probe_tick(frames, is_dialog, tab_index, tabs, current, keys_down);
    return;
#endif
    const size_t count = sizeof(MENU_HELP_PROBE_CASES) / sizeof(MENU_HELP_PROBE_CASES[0]);
    if (menu_help_probe_case >= count) {
        return;
    }
    ++frames;
    if (!is_dialog && frames == 1) {
        const MenuHelpProbeCase& target = MENU_HELP_PROBE_CASES[menu_help_probe_case];
        bool found = false;
        for (size_t t = 0; t < tabs.size(); ++t) {
            if (tabs[t].TabId != target.tab) {
                continue;
            }
            for (size_t row = 0; row < tabs[t].MenuItems.size(); ++row) {
                if (tabs[t].MenuItems[row].Text != target.label) {
                    continue;
                }
                tab_index = static_cast<int>(t);
                current = &tabs[t];
                current->SelectedItemIndex = static_cast<int>(row);
                current->FirstItemIndex = row > 2 ? static_cast<int>(row) - 2 : 0;
                secondScreenDirty = true;
                found = true;
                break;
            }
        }
        if (!found) {
            log3dsWrite("[menu-help-probe] missing case %u", static_cast<unsigned>(menu_help_probe_case));
        }
    }
    // The idle menu only redraws the dirty back buffer. Refresh both
    // buffers before reading gfxGetFramebuffer, which returns the back one.
    if (frames == 28 || frames == 29) {
        secondScreenDirty = true;
    }
    if (frames == 30) {
        char path[128];
        const int n = snprintf(path, sizeof(path),
            "sdmc:/3ds/snes9x_3ds/menu_help_%u_%s.ppm",
            static_cast<unsigned>(menu_help_probe_case), is_dialog ? "help" : "menu");
        const bool ok = n > 0 && static_cast<size_t>(n) < sizeof(path) &&
            menu_help_probe_save(path);
        log3dsWrite("[menu-help-probe] case %u %s: %s",
            static_cast<unsigned>(menu_help_probe_case), is_dialog ? "help" : "menu", ok ? "ok" : "FAILED");
    }
    if (frames == 31) {
        keys_down = is_dialog ? KEY_B : KEY_SELECT;
    }
    if (!is_dialog && frames == 32) {
        ++menu_help_probe_case;
        frames = 0;
    }
}
