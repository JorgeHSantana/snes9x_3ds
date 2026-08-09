# MSU-1 Menu Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Menu-facing MSU-1 controls: per-game volume multiplier with a global default, per-game enable/disable applied live, and a humanized status line styled like the menu's existing informational text.

**Architecture:** New logic lives in the host-testable bridge (`source/3dsmsu.{h,cpp}`): user-volume factor in the mix formula, status formatter, enable-gate decision helper. Platform files get thin anchor-directed edits (settings fields, config keys, menu items, call sites). Spec: `docs/superpowers/specs/2026-08-09-msu1-menu-settings-design.md`.

**Tech Stack:** Same as wave 1 (C++17 no-exceptions/no-RTTI production code, doctest host tests, devkitARM target via Docker).

## Global Constraints

- Follow `docs/CODING_STANDARD.md` (naming, validation, no runtime allocation, status returns).
- Branch: `feature/msu1` (continue on it). Host suite must stay green: `make -C tests`.
- Volume factor curve: `user_factor = gauge_value * 0.25f` (gauge 0-8; 4 = 1.0 neutral, 0 = mute, 8 = 2.0).
- Mix formula becomes: `global_volume * user_factor * (MSU1.volume / 255.0f)`, still zeroed by mute flags.
- Status thresholds: `MSU1_STUTTER_MINOR_THRESHOLD = 1`, `MSU1_STUTTER_SEVERE_THRESHOLD = 6` (named constants in `3dsmsu.h`).
- Status strings (exact): `"MSU-1: playing track %u"`, `"MSU-1: detected"`, `"MSU-1: not detected"`; subtitles `"Minor audio stutter detected"`, `"Audio is stuttering - a faster SD card may help"` (ASCII hyphen). Raw underrun count NEVER appears in UI.
- Config version bumps: `GLOBAL_CONFIG_FILE_TARGET_VERSION` 1.6f → 1.7f, `GAME_CONFIG_FILE_TARGET_VERSION` 1.5f → 1.6f (`source/3dsconfig.h:11-12`).
- Settings: per-game `Msu1Enabled` (default true), per-game `Msu1Volume` (0-8, seeded from global default), global `Msu1VolumeDefault` (0-8, default 4).
- 3dsconfig.cpp is NOT host-compilable (`3dsconfig.h` includes `3ds.h`): the new config keys are one-line delegating edits following the file's existing per-key pattern — no host tests for them (established touched-code interpretation); all NEW logic (formatter, volume, gate) is host-tested.
- **Visual requirement (maintainer-mandated):** the status menu item must reuse the menu's existing muted/informational styling and look correct in all 3 themes; the implementer's report must name the existing menu item used as visual reference.
- Legacy platform files (3dsmain/3dsimpl/3dssettings/3dsconfig) cannot be compiled locally — read anchors first, match file-local style, keep edits minimal; target build happens in the final task via Docker.

---

### Task 1: Bridge user-volume factor

**Files:**
- Modify: `source/3dsmsu.h`, `source/3dsmsu.cpp`
- Create: `tests/test_msu_user_volume.cpp` (add to `TEST_SRCS` in tests/Makefile)

**Interfaces:**
- Consumes: existing bridge (`g_bridge`, `apply_mix`, `msu3dsSetGlobalVolume`, fake backend `fake::last_mix`).
- Produces: `void msu3dsSetUserVolume(float factor);` — clamps to [0.0, 2.0], stores in bridge state (default **1.0** at init), triggers `apply_mix()`; guarded on `initialized` for the apply but the value is stored only when initialized (mirror `msu3dsSetGlobalVolume`'s guard exactly). `apply_mix()` becomes `global_volume * user_volume * (MSU1.volume / 255.0f)` (zero when muted).

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_msu_user_volume.cpp
#include "doctest.h"
#include "3dsmsu.h"
#include "fake_backend.h"

static int16_t staging[fake::CAP_SAMPLES * 2];

static void fresh()
{
    fake::reset();
    msu3dsFinalize();
    REQUIRE(msu3dsInitialize(fake::make(), staging, fake::CAP_SAMPLES));
    MSU1 = Msu1State{};
    MSU1.enabled = true;
    MSU1.volume = 255;
    msu3dsSetGlobalVolume(1.0f);
}

TEST_CASE("user volume defaults to neutral 1.0")
{
    fresh();
    msu3dsOnEvent(Msu1Event::VolumeChanged);
    CHECK(fake::last_mix == doctest::Approx(1.0f));
}

TEST_CASE("user volume multiplies into the mix")
{
    fresh();
    msu3dsSetUserVolume(0.5f);
    CHECK(fake::last_mix == doctest::Approx(0.5f));
    MSU1.volume = 128;
    msu3dsSetUserVolume(2.0f);
    CHECK(fake::last_mix == doctest::Approx(2.0f * 128.0f / 255.0f));
}

TEST_CASE("user volume clamps and stacks with global; mute still wins")
{
    fresh();
    msu3dsSetUserVolume(99.0f);              // clamps to 2.0
    msu3dsSetGlobalVolume(1.5f);
    CHECK(fake::last_mix == doctest::Approx(1.5f * 2.0f));
    msu3dsSetUserVolume(-1.0f);              // clamps to 0.0
    CHECK(fake::last_mix == doctest::Approx(0.0f));
    msu3dsSetUserVolume(1.0f);
    msu3dsOnEvent(Msu1Event::MenuEnter);
    CHECK(fake::last_mix == doctest::Approx(0.0f));
    msu3dsOnEvent(Msu1Event::MenuExit);
    CHECK(fake::last_mix == doctest::Approx(1.5f));
}

TEST_CASE("uninitialized bridge: safe no-op")
{
    msu3dsFinalize();
    msu3dsSetUserVolume(0.5f);               // must not crash
}
```

- [ ] **Step 2:** `make -C tests` → RED (undefined `msu3dsSetUserVolume`). Capture.
- [ ] **Step 3:** Implement: `user_volume` field in `BridgeState` (initialized to 1.0f in `msu3dsInitialize`), setter mirroring `msu3dsSetGlobalVolume` (clamp + guard + `apply_mix()`), formula update in `apply_mix`.
- [ ] **Step 4:** `make -C tests` → GREEN full suite.
- [ ] **Step 5:** Commit `feat(msu1): user volume factor in bridge mix formula` + trailer.

---

### Task 2: Humanized status formatter

**Files:**
- Modify: `source/3dsmsu.h`, `source/3dsmsu.cpp`
- Create: `tests/test_msu_status.cpp` (add to `TEST_SRCS`)

**Interfaces:**
- Produces (in `3dsmsu.h`):

```cpp
inline constexpr uint32_t MSU1_STUTTER_MINOR_THRESHOLD  = 1;
inline constexpr uint32_t MSU1_STUTTER_SEVERE_THRESHOLD = 6;

// Formats the menu status line (+ optional warning subtitle) from chip state.
// line/subtitle are always NUL-terminated on success; subtitle becomes ""
// when there is no warning. Returns false on invalid args (null/zero sizes).
bool msu3dsFormatStatus(bool msu_present, const Msu1State& state,
                        uint32_t underruns,
                        char* line, size_t line_size,
                        char* subtitle, size_t subtitle_size);
```

Truth table (spec §6): not present → `MSU-1: not detected`; present+PLAYING → `MSU-1: playing track %u` (current_track); present idle → `MSU-1: detected`. Subtitle: underruns == 0 → `""`; 1-5 → `Minor audio stutter detected`; >=6 → `Audio is stuttering - a faster SD card may help`. Subtitle applies only when msu_present.

- [ ] **Step 1: Write failing tests** — all truth-table rows + boundary values 0/1/5/6 + garbage args (nullptr line, zero sizes, tiny buffers → false, no overflow) + track number formatting (`current_track = 42` appears in line).
- [ ] **Step 2:** RED. **Step 3:** Implement with `snprintf` overflow checks per the standard. **Step 4:** GREEN. **Step 5:** Commit `feat(msu1): humanized status formatter with stutter thresholds` + trailer.

---

### Task 3: Enable-gate decision helper + live-apply platform function

**Files:**
- Modify: `source/3dsmsu.h`, `source/3dsmsu.cpp` (decision helper — host-tested)
- Modify: `source/3dsimpl.h`, `source/3dsimpl.cpp` (platform live-apply — 3DS-only, thin)
- Create: `tests/test_msu_enable_gate.cpp` (add to `TEST_SRCS`)

**Interfaces:**
- Produces (bridge, host-tested):

```cpp
enum class Msu1EnableAction : uint8_t { None, TearDown, Detect };
// setting_enabled: the per-game Msu1Enabled setting; chip_active: Settings.MSU1 truth
Msu1EnableAction msu3dsDecideEnableAction(bool setting_enabled, bool chip_active);
```
Truth table: enabled+active → None; enabled+inactive → Detect; disabled+active → TearDown; disabled+inactive → None.

- Produces (platform, `3dsimpl.h`): `void impl3dsApplyMsu1Enable(bool enabled);` — computes the action via the helper and executes inside a drain fence: TearDown = `snd3dsDrainMixing(); msu3dsOnEvent(Msu1Event::RomUnload)`-equivalent teardown (`S9xMSU1Shutdown(); Settings.MSU1 = FALSE;` + bridge queue clear via the existing event) `; snd3dsResumeMixing();`. Detect = drain; `msu1_detect(Memory.ROMFilename) && msu1_init(MSU1, Memory.ROMFilename) == Msu1Result::Ok` → `Settings.MSU1 = TRUE`; resume. **Read the RomUnload event's existing behavior first and reuse it rather than duplicating teardown logic** — implementer documents the exact composition chosen.

- [ ] **Step 1:** Failing tests for the 4-row decision table (trivial but locks the contract).
- [ ] **Step 2:** RED → **Step 3:** implement helper → GREEN.
- [ ] **Step 4:** Platform function in 3dsimpl.cpp (read anchors: `impl3dsResetConsole` shows the drain-fence pattern; `memmap.cpp`'s detection block shows the detect+init idiom). No host test (3DS-only); symbol-check in report.
- [ ] **Step 5:** Full suite green; commit `feat(msu1): enable-gate decision helper and live-apply` + trailer.

---

### Task 4: Settings fields, defaults seeding, config keys, version bumps

**Files:**
- Modify: `source/3dssettings.h` (fields), `source/3dssettings.cpp` (defaults + `settings3dsUpdate` hookup), `source/3dsconfig.h` (version bumps), `source/3dsmain.cpp` (config read/write lists)

All edits are anchor-directed, file-local style, no host tests (delegating pattern — see Global Constraints):

- [ ] **Step 1:** `3dssettings.h` — add to the per-game section: `int Msu1Volume = 4;` and `bool Msu1Enabled = true;`; to the global section: `int Msu1VolumeDefault = 4;` (match the header's existing field style — read it first; if fields there aren't default-initialized inline, set defaults in the reset functions instead, mirroring neighbors).
- [ ] **Step 2:** `3dssettings.cpp` — in `settings3dsResetGameDefaults()`: `Msu1Enabled = true; Msu1Volume = settings3DS.Msu1VolumeDefault;` (seeding). In `settings3dsResetGlobalDefaults()`: `Msu1VolumeDefault = 4;`. In `settings3dsUpdate()` where volume is applied (near the `snd3dsApplyOutputVolume` call): `msu3dsSetUserVolume((float)settings3DS.Msu1Volume * 0.25f);` (include `"3dsmsu.h"`).
- [ ] **Step 3:** `3dsconfig.h:11-12` — bump both version constants (1.7f / 1.6f).
- [ ] **Step 4:** `3dsmain.cpp` — in `settingsReadWriteFullListByGame` (starts ~line 1221): add, following the exact one-line pattern of neighbors:
  ```cpp
  config3dsReadWriteInt32(stream, writeMode, "Msu1Enabled=%d\n", (int32*)&settings3DS.Msu1Enabled, 0, 1);
  config3dsReadWriteInt32(stream, writeMode, "Msu1Volume=%d\n", &settings3DS.Msu1Volume, 0, 8);
  ```
  (CONFIRM the actual parameter shapes from neighboring calls — bool fields elsewhere in the file show the established cast/route; mirror exactly.) In the global list function: `config3dsReadWriteInt32(stream, writeMode, "Msu1VolumeDefault=%d\n", &settings3DS.Msu1VolumeDefault, 0, 8);`
- [ ] **Step 5:** Post-config enable gate — in `3dsmain.cpp`'s `emulatorLoadRom()` AFTER the per-game config read (read the function; wave-1 placed RomUnload before the load — this new line goes after config load): `if (!settings3DS.Msu1Enabled) { impl3dsApplyMsu1Enable(false); }` (include already present).
- [ ] **Step 6:** `make -C tests` still green (proves no shared-header breakage); commit `feat(msu1): settings fields, config keys, defaults seeding` + trailer.

---

### Task 5: Menu section + status item (THE VISUAL TASK)

**Files:**
- Modify: `source/3dsmain.cpp` (`makeOptionMenu`, ~line 761)
- Modify: `docs/msu1.md` (new "Settings" section)

- [ ] **Step 1:** READ `makeOptionMenu` and the `SMenuItem` type list (`3dsmenu.h`) fully. Identify: how sections are headed (`Header1`/`Header2`), how checkboxes/gauges bind callbacks, and **which existing item renders secondary/informational text in the muted style** (candidates: `Textarea`, `Disabled`, or a subtitle mechanism — pick what other informational rows actually use). Name the reference item in the report.
- [ ] **Step 2:** Add the "MSU-1" section to the Options tab, in this order:
  1. Status line: build via `msu3dsFormatStatus(Settings.MSU1, MSU1, msu3dsGetUnderrunCount(), ...)` at menu-build time (tab rebuilds on entry via existing dirty flags); render line + subtitle using the reference item's style. NON-INTERACTIVE.
  2. Checkbox "Enable MSU-1" bound to `settings3DS.Msu1Enabled`; change-callback calls `impl3dsApplyMsu1Enable(value != 0)` and marks the tab dirty (so the status line refreshes).
  3. Gauge "MSU-1 Volume" (0-8) bound to `settings3DS.Msu1Volume`; callback applies `msu3dsSetUserVolume(value * 0.25f)` live.
  4. Gauge "MSU-1 Default Volume" (0-8) bound to `settings3DS.Msu1VolumeDefault` (no live effect; seeds future games).
- [ ] **Step 3:** docs/msu1.md — add a short "Settings" section documenting the three controls and the status line meanings.
- [ ] **Step 4:** `make -C tests` green; commit `feat(msu1): MSU-1 menu section with humanized status` + trailer. Report MUST name the visual reference item and confirm the styling path works in all 3 themes (by reading the theme color usage of that item type).

---

### Task 6: Target build + wrap-up

- [ ] **Step 1:** `docker run --rm --platform linux/amd64 -v "$PWD":/snes9x_3ds -w /snes9x_3ds devkitpro/devkitarm make release` — must compile clean under `-Werror` (this is the first compile of Tasks 3-5's platform edits). Fix any compile errors as part of this task (small fix commits).
- [ ] **Step 2:** `make -C tests` full suite one last time.
- [ ] **Step 3:** Commit any remaining changes; push `feature/msu1`.

## Execution notes

- Tasks 1-3 are pure host-side TDD; Tasks 4-5 are anchor-directed legacy edits (read first, mirror style); Task 6 is the compile gate for everything the host build can't see.
- Menu visual check on real hardware rides along with the wave-1 hardware validation the maintainer is already running (add: open the Options tab in all 3 themes).
