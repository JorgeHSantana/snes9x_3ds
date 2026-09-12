// Included by the menu capture harness only in explicit probe builds.
#include "gfxhw.h"
static uint32_t LAYER_TOGGLE_PROBE_VISIT = 0;

static void layer_toggle_probe_tick(int& frames, bool is_dialog, int& tab_index,
                                   std::vector<SMenuTab>& tabs, SMenuTab*& current,
                                   u32& keys_down)
{
    if (is_dialog || LAYER_TOGGLE_PROBE_VISIT >= 2) {
        return;
    }
    ++frames;
    if (frames == 1) {
        bool found = false;
        for (size_t tab = 0; tab < tabs.size(); ++tab) {
            if (tabs[tab].TabId != TAB_3D) {
                continue;
            }
            tab_index = static_cast<int>(tab);
            current = &tabs[tab];
            for (size_t row = 0; row < current->MenuItems.size(); ++row) {
                if (current->MenuItems[row].Text == "  BG1") {
                    current->SelectedItemIndex = static_cast<int>(row);
                    current->FirstItemIndex = row > 2 ? static_cast<int>(row) - 2 : 0;
                    found = true;
                    break;
                }
            }
        }
        log3dsWrite("[layer-toggle-probe] visit=%u found=%d enabled=%d used=%d hide=%d",
            static_cast<unsigned>(LAYER_TOGGLE_PROBE_VISIT), found,
            settings3DS.LayerEnabled[0], S9xLayerUsedLastFrameAny(0), settings3DS.StereoHideUnused);
        if (!found) {
            LAYER_TOGGLE_PROBE_VISIT = 2; // Missing control must fail the runner.
            return;
        }
        secondScreenDirty = true;
    }
    if (frames == 28 || frames == 29 || frames == 58 || frames == 59) {
        secondScreenDirty = true;
    }
    if (frames == 30 || frames == 60) {
        const uint32_t capture = LAYER_TOGGLE_PROBE_VISIT * 2 + (frames == 60 ? 1 : 0);
        const bool expected_enabled = capture == 0 || capture == 3;
        if ((settings3DS.LayerEnabled[0] != 0) != expected_enabled) {
            log3dsWrite("[layer-toggle-probe] FAILED checkbox state at capture %u",
                        static_cast<unsigned>(capture));
            LAYER_TOGGLE_PROBE_VISIT = 2;
            return;
        }
        char path[128];
        const int result = snprintf(path, sizeof(path), "sdmc:/3ds/snes9x_3ds/menu_layer_%u.ppm",
                                    static_cast<unsigned>(capture));
        const bool ok = result > 0 && static_cast<size_t>(result) < sizeof(path) &&
                        menu_help_probe_save(path);
        log3dsWrite("[layer-toggle-probe] capture=%u enabled=%d saved=%d",
                    static_cast<unsigned>(capture), settings3DS.LayerEnabled[0], ok);
    }
    if (frames == 31) {
        keys_down = KEY_A; // Use the real checkbox handling and preview callback.
    }
    if (frames == 61) {
        ++LAYER_TOGGLE_PROBE_VISIT;
        if (LAYER_TOGGLE_PROBE_VISIT == 1) {
            keys_down = KEY_B; // Resume the ROM; impl3ds reopens after 120 frames.
        }
    }
}
