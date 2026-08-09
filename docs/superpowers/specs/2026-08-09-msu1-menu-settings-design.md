# MSU-1 Menu Settings — Design

Date: 2026-08-09
Status: approved (brainstorming session with maintainer)
Depends on: MSU-1 wave 1 (branch `feature/msu1`, complete). Target branch: `feature/msu1` (continues the same branch; merge decision still pending hardware validation).

## 1. Goal

User-facing MSU-1 controls in the emulator menu: per-game volume balancing with a global default, per-game enable/disable applied live, and a humanized status line — all visually consistent with the existing menu.

## 2. Settings model

| Setting | Scope | Type / range | Default | Meaning |
|---|---|---|---|---|
| `Msu1Enabled` | per-game | bool | on | Off = the chip is torn down / not detected; the game falls back to normal SNES audio (hacks have fallback) |
| `Msu1Volume` | per-game | int 0-8 (gauge) | seeded from global default | User multiplier over the game's own `$2006` volume: factor = `value × 0.25` (0 = mute, **4 = 1.0× neutral**, 8 = 2.0×) |
| `Msu1VolumeDefault` | global | int 0-8 (gauge) | 4 | Seeds `Msu1Volume` for games without a config file |

Config persistence via the existing versioned key mechanism: bump global config target version 1.6 → 1.7 and per-game 1.5 → 1.6; new keys are skipped when reading older files (existing behavior), always written.

## 3. Mix formula (bridge)

```
channel-1 mix = global_volume_factor × msu_user_factor × (MSU1.volume / 255)
```

New bridge API `msu3dsSetUserVolume(float factor)` (clamped [0, 2], guarded on initialized, host-tested), called from `settings3dsUpdate()` alongside the existing `msu3dsSetGlobalVolume` path. The game's `$2006` fades keep working; the user factor only rebalances MSU vs SNES channels.

## 4. Enable/disable semantics

* **Load-time gate**: the per-game config is read *after* ROM-load detection runs, so the gate executes platform-side after config read: if `!Msu1Enabled && Settings.MSU1` → drain fence + `S9xMSU1Shutdown()` + `Settings.MSU1 = FALSE`.
* **Live toggle from the menu**: the checkbox applies immediately via a platform helper (`msu3dsApplyEnableSetting()` or equivalent): disabling = drain + shutdown + flag off; enabling = drain + re-run detection/init (same code path as ROM load). No game reload required. All inside the existing drain fences.

## 5. Menu (Options tab, new "MSU-1" section)

Items in order:
1. **Status line** — non-interactive informational item (see §6).
2. **Checkbox** "Enable MSU-1" (per-game).
3. **Gauge** "MSU-1 Volume" (per-game, 0-8).
4. **Gauge** "MSU-1 Default Volume" (global, 0-8).

Section always visible (status shows "not detected" for non-MSU games; the three controls remain functional — settings apply to future detections).

## 6. Humanized status line — with a hard visual-quality requirement

Content (snapshot taken when the menu tab is (re)built — menu entry marks tabs dirty already):

| Session state | Line (+ subtitle when applicable) |
|---|---|
| Healthy, playing | `MSU-1: playing track 3` |
| Detected, idle | `MSU-1: detected` |
| 1-5 underruns this session | + subtitle `Minor audio stutter detected` |
| >5 underruns | + subtitle `Audio is stuttering — a faster SD card may help` |
| No MSU for this game | `MSU-1: not detected` |

Thresholds are named constants (`MSU1_STUTTER_MINOR_THRESHOLD = 1`, `MSU1_STUTTER_SEVERE_THRESHOLD = 6`), expected to be recalibrated after Old 3DS hardware testing. **The raw underrun count never appears in the UI** — it stays in the debug log (already implemented).

**Visual-quality requirement (maintainer-mandated):** the status item MUST reuse the menu's existing informational styling — the muted/darker font used by non-selectable/subtitle text — and MUST render correctly in all 3 themes (Dark, RetroArch, Original). No new colors, no new item type unless an existing one (`Disabled`/`Textarea` + subtitle mechanism) genuinely cannot express it. The implementation report must name which existing menu item was used as the visual reference. If in doubt, match how the menu renders other secondary/informational text, not headers.

## 7. Testing

* Bridge: mix formula with the new user factor (fake backend; neutral=1.0, mute=0, 2.0 cap, interaction with mute states and global factor).
* Config: round-trip of the three new keys; version-skip behavior for old files (existing test pattern if present, else new focused tests of `config3dsReadWrite*` usage).
* Enable gate: the decision logic extracted into a host-testable helper (given enabled-flag + detection state → action enum), platform call sites stay thin.
* Status line: pure formatting function `msu1_format_status(...)` (host-tested: all 5 table rows, threshold boundaries at 0/1/5/6).
* Menu/UI wiring itself: thin, follows existing patterns; visual check is manual (all 3 themes).

## 8. Out of scope

Wave-2 items (FMV/data throughput, DMA modes 1-4) — separate spec after the research report. No per-game `UseGlobal*` switch for MSU volume (the global default + per-game override covers the need without the extra machinery).
