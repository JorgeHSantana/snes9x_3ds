# MSU-1 Audio-Section Refactor — Design

Date: 2026-08-09
Status: approved (maintainer-proposed design)
Supersedes: parts of `2026-08-09-msu1-menu-settings-design.md` (§4 live toggle, §5 section layout, Msu1VolumeDefault)

## 1. Goal and rationale

Maintainer-driven redesign after hardware testing revealed:
- **Live enable/disable is dishonest and hazardous**: MSU hacks detect the chip at boot only, and audio-only hacks (e.g. Mega Man X3 MSU) REPLACE the SPC music code — fallback mid-game is impossible. The live-apply path was also the machinery running during the unreproduced missing-layers incident.
- The menu should integrate MSU volume into a **unified Audio section**, reuse the **existing global-vs-per-game switch**, and show the humanized status at the section's bottom in the muted style.

## 2. Menu (Options tab) — the new Audio section

Replace the standalone "MSU-1" section with a Header2 **"Audio"** section:

1. **"SNES Volume"** — the existing per-game volume gauge, relabeled (was "Volume"; wording final: `SNES Volume`).
2. **"MSU-1 Volume"** (gauge 0-8, ×0.25 curve, live-applied) — **shown ONLY when `Settings.MSU1` is true** (safe: with live-apply gone, `Settings.MSU1` is constant within a session, so item count cannot change mid-session; ROM switches force full menu rebuilds already).
3. **"Enable MSU-1"** (checkbox) — shown when `Settings.MSU1 || !settings3DS.Msu1Enabled` (visible for MSU games, and for games disabled-by-setting so the user can re-enable). Description text: `Applies when the game is loaded or reset.` **No live-apply**: the callback only stores the setting + marks the tab dirty.
4. **Status line + subtitle at the section bottom** — the existing humanized formatter, muted Textarea styling, constant two-row footprint. Unchanged content rules ("playing track N" / "detected" / "disabled" / "not detected" + stutter subtitles).

The existing **`UseGlobalVolume`** switch (already in the menu) now governs BOTH volumes: when on, the global SNES volume AND the global MSU-1 volume apply; when off, the per-game values apply. Its description is updated to say so.

## 3. Settings model changes

| Setting | Change |
|---|---|
| `Msu1Volume` (per-game) | kept |
| `Msu1Enabled` (per-game) | kept; applied ONLY by the load-time gate (already in `emulatorLoadRom`) |
| `Msu1VolumeDefault` (global) | **REMOVED** — replaced by `GlobalMsu1Volume` (int 0-8, default 4), the MSU counterpart of `GlobalVolume`, governed by `UseGlobalVolume` |

Effective MSU user factor in `settings3dsUpdate()`:
`(UseGlobalVolume ? GlobalMsu1Volume : Msu1Volume) * 0.25f` → `msu3dsSetUserVolume(...)`.
Per-game seeding: `settings3dsResetGameDefaults()` seeds `Msu1Volume = GlobalMsu1Volume`.

## 4. Config migration (sequential-parse discipline!)

Global config bumps **1.7 → 1.8**:
- Write `GlobalMsu1Volume=%d` in a `>= 1.8` block at the position previously used by `Msu1VolumeDefault`.
- **Reading a v1.7 file** (exists on the maintainer's SD): the `Msu1VolumeDefault=` line MUST be consumed to keep the sequential parse aligned — read it **into `GlobalMsu1Volume`** (semantic migration: the old default becomes the new global volume). Guard shape: `>= 1.7` reads whichever key name matches that file's version; implementation may read the old key name for `>= 1.7f && < 1.8f` files and the new name for `>= 1.8f`.
- Game config version stays 1.6 (`Msu1Enabled`/`Msu1Volume` unchanged).

## 5. Code removals (the simplification payoff)

- `impl3dsApplyMsu1Enable` (3dsimpl.h/.cpp) — deleted. Call sites deleted: Reset Config callback (config reset simply takes effect at next load, like every other per-game setting), checkbox callback.
- `msu3dsDecideEnableAction` + `Msu1EnableAction` (3dsmsu.h/.cpp) — deleted, with their tests (`tests/test_msu_enable_gate.cpp` removed from TEST_SRCS and deleted).
- `Msu1VolumeDefault` field + its gauge.
- KEPT: the inline load-time gate in `emulatorLoadRom` (drain-fenced, verified), `msu3dsSetUserVolume`, `msu3dsFormatStatus`, MenuEnter dirty-mark, all wave-1 machinery.

## 6. Testing

- Update `tests/test_msu_user_volume.cpp` only if signatures change (they don't).
- Formatter tests unchanged.
- Delete enable-gate tests with the helper.
- Menu/settings/config edits: anchor-directed platform changes (not host-compilable), verified by suite-green + Docker `-Werror` build + on-device check.
- docs/msu1.md: Settings section rewritten (Audio section, global switch behavior, "applies on load/reset" honesty, note that hacks without fallback go silent when disabled).

## 7. Visual requirement (unchanged, maintainer-mandated)

Status rows keep the muted `Textarea` styling verified in all 3 themes; the Audio section must look native next to the existing sections; constant item count within a session.
