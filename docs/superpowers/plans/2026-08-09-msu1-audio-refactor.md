# MSU-1 Audio-Section Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unified Audio menu section (SNES + MSU-1 volumes, existing global/per-game switch), honest enable checkbox (applies on load/reset, no live-apply), status footer — deleting the live-apply machinery.

**Architecture:** Spec: `docs/superpowers/specs/2026-08-09-msu1-audio-section-refactor-design.md`. Mostly anchor-directed platform edits + deletions; the only host-tested surface changes are deletions (enable-gate tests) — suite must stay green throughout.

**Tech Stack:** unchanged (doctest host suite, Docker devkitARM target build).

## Global Constraints

- Branch `feature/msu1`. `make -C tests` green after every task. Coding standard applies.
- Global config version 1.7 → 1.8 with the **sequential-parse migration**: v1.7 files' `Msu1VolumeDefault=` line is consumed INTO `GlobalMsu1Volume`; v1.8 files read/write `GlobalMsu1Volume=`. Game config stays 1.6.
- Effective MSU factor: `(UseGlobalVolume ? GlobalMsu1Volume : Msu1Volume) * 0.25f`.
- Menu conditionals may depend only on session-constant state (`Settings.MSU1`, `Msu1Enabled` at build time) — item count constant within a session.
- Enable checkbox description: `Applies when the game is loaded or reset.`
- Status rows keep muted Textarea styling; section must render native in all 3 themes.
- Legacy platform files not host-compilable: read anchors first, mirror style, report hunks; Docker build is the compile gate (final task).

---

### Task 1: Settings + config migration (GlobalMsu1Volume)

**Files:** Modify `source/3dssettings.h`, `source/3dssettings.cpp`, `source/3dsconfig.h`, `source/3dsmain.cpp` (global config list only).

- [ ] Step 1: `3dssettings.h` — rename field `Msu1VolumeDefault` → `GlobalMsu1Volume` (same type/range; sits with the other Global* fields; keep per-game `Msu1Volume`/`Msu1Enabled` untouched).
- [ ] Step 2: `3dssettings.cpp` — `settings3dsResetGlobalDefaults()`: `GlobalMsu1Volume = 4;`. `settings3dsResetGameDefaults()`: seed `Msu1Volume = settings3DS.GlobalMsu1Volume;`. `settings3dsUpdate()`: replace the user-volume line with `msu3dsSetUserVolume((float)(settings3DS.UseGlobalVolume ? settings3DS.GlobalMsu1Volume : settings3DS.Msu1Volume) * 0.25f);` — read the surrounding code to confirm how `UseGlobalVolume` gates the existing SNES volume application and MIRROR that exact condition source (it may consult a helper or the Global/per-game copy pattern; report what you found).
- [ ] Step 3: `3dsconfig.h` — `GLOBAL_CONFIG_FILE_TARGET_VERSION` 1.7f → 1.8f (game version untouched).
- [ ] Step 4: `3dsmain.cpp` global list — replace the `>= 1.7f` Msu1VolumeDefault block with:
  ```cpp
  if (writeMode || detectedConfigVersion >= 1.8f) {
      config3dsReadWriteInt32(stream, writeMode, "GlobalMsu1Volume=%d\n", &settings3DS.GlobalMsu1Volume, 0, 8);
  } else if (!writeMode && detectedConfigVersion >= 1.7f) {
      // migrate v1.7's Msu1VolumeDefault (same position in the sequence) into GlobalMsu1Volume
      config3dsReadWriteInt32(stream, writeMode, "Msu1VolumeDefault=%d\n", &settings3DS.GlobalMsu1Volume, 0, 8);
  }
  ```
  (Confirm the exact call shape against neighbors; the migration branch is read-only by construction.)
- [ ] Step 5: `make -C tests` green; commit `refactor(msu1): GlobalMsu1Volume with v1.7 config migration` + trailer.

### Task 2: Delete the live-apply machinery

**Files:** Modify `source/3dsimpl.h`, `source/3dsimpl.cpp`, `source/3dsmsu.h`, `source/3dsmsu.cpp`, `source/3dsmain.cpp` (Reset Config callback), `tests/Makefile`; Delete `tests/test_msu_enable_gate.cpp`.

- [ ] Step 1: Delete `impl3dsApplyMsu1Enable` (declaration + definition + doc comment). Delete its Reset Config call site (the whole comment+call added by commit d35fe65). KEEP the inline load gate in `emulatorLoadRom` (do not touch it).
- [ ] Step 2: Delete `Msu1EnableAction` + `msu3dsDecideEnableAction` from 3dsmsu.h/.cpp.
- [ ] Step 3: Delete `tests/test_msu_enable_gate.cpp`, remove from TEST_SRCS.
- [ ] Step 4: `make -C tests` clean rebuild green (case count drops by the deleted suite); grep the tree for any残 leftover references (`ApplyMsu1Enable\|Msu1EnableAction\|DecideEnableAction`) — must be zero outside docs/history.
- [ ] Step 5: Commit `refactor(msu1): remove live enable-apply machinery` + trailer.

### Task 3: Menu — the unified Audio section

**Files:** Modify `source/3dsmain.cpp` (makeOptionMenu + checkbox callback), `docs/msu1.md`.

- [ ] Step 1 (discovery): read makeOptionMenu fully. Locate the existing volume gauge item (its label, section, and the `UseGlobalVolume` switch's location/description elsewhere in the menus). Locate the current standalone "MSU-1" section (added by the previous feature).
- [ ] Step 2: Restructure: Header2 `"Audio"` section containing, in order: the existing volume gauge relabeled `"  SNES Volume"`; `"  MSU-1 Volume"` gauge added ONLY when `Settings.MSU1` (callback keeps live `msu3dsSetUserVolume(val * 0.25f)` — but ONLY when this per-game value is active, i.e. mirror the UseGlobalVolume condition: when global is in use, the per-game gauge still stores but must not clobber the active factor; simplest correct: after `CheckAndUpdate`, call `settings3dsUpdate`-equivalent recompute — read how the SNES volume gauge callback applies its change and MIRROR it exactly); `"  Enable MSU-1"` checkbox when `Settings.MSU1 || !settings3DS.Msu1Enabled`, callback = store + `menu3dsMarkTabDirty(TAB_SETTINGS)` only, description `Applies when the game is loaded or reset.`; the two status Textarea rows (moved from the old section, unchanged construction).
- [ ] Step 3: Delete the old standalone "MSU-1" section entirely (header, gauges incl. Default Volume, old checkbox). Update the `UseGlobalVolume` switch's description text (wherever it lives) to mention it now also governs MSU-1 volume.
- [ ] Step 4: docs/msu1.md — rewrite the Settings section per the new layout; add the honesty note: disabling MSU-1 on hacks without an SPC fallback means silence.
- [ ] Step 5: `make -C tests` green; commit `refactor(msu1): unified Audio menu section` + trailer. Report every hunk + confirm item-count constancy per session and the styling reuse.

### Task 4: Build, review, push

- [ ] Step 1: Docker `make release` — clean under -Werror.
- [ ] Step 2: Full suite; final scoped review of the refactor branch delta (the three commits) by a reviewer subagent (spec-conformance + the config-migration correctness is the highest-risk item: hand-trace a v1.7 file read).
- [ ] Step 3: Fix anything found (fix loop); push; update memory/ledger.

---

## Completion Notes (2026-08-09)

4 tasks + final review + text-fix wave (6 commits, a1fbeed..bbdc783). Suite 78/476; Docker -Werror clean. Final review verified: v1.7→1.8 config migration hand-traced, volume semantics coherent across all toggle sequences, session-latched menu visibility, savestate/gate interactions hold. Deferred: transient "not detected" status after mid-session re-enable (documented); UseGlobalVolume toggle promotes invisible per-game MSU volume on non-MSU games (mirrors SNES semantics); downgrade to older builds self-heals with defaults.
