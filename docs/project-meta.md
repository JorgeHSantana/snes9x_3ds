# Project Meta — CI, Releases, Known Issues, Licensing

[← Back to index](README.md)

## CI (`.github/workflows/`)

| Workflow | Trigger | What it does |
|---|---|---|
| `ci.yml` | push to `develop`, PRs to `develop`/`master` | `devkitpro/devkitarm` container → `make release`. No tests, no artifacts |
| `master-build.yml` | push to `master` | Stamps the app title with the short SHA (`Snes9x for 3DS (<sha>)`), `make release`, uploads `output/` as an artifact |
| `pr-base-policy.yml` | `pull_request_target` | PRs targeting `master` are rejected unless the author has write/maintain/admin permission — contributors must target `develop` |

## Release process

* **Manual** — no tag/release workflow. Work lands on `develop`; merges to `master` produce CI artifacts; tagged GitHub releases (`v1.48` … `v1.61`) on matbo87/snes9x_3ds are the official stable builds.
* Install on console via Universal Updater or the `.cia` from Releases; 3DSX users copy `snes9x_3ds.3dsx` to `sd:/3ds/snes9x_3ds`.

## Contribution policy (from README)

* Target `develop`, never `master` directly.
* **AI-assisted code is accepted, but contributors must understand and validate it**; overly broad/risky PRs may be closed or split. The maintainer discloses using AI assistants for review/debugging/planning/implementation/docs.

## Git context of this checkout

* Remotes: `origin` = JorgeHSantana/snes9x_3ds (personal fork), `upstream` = matbo87/snes9x_3ds, `bubble2k16` = original project.
* ~620 commits; principal authors: matbo87 (~310), bubble2k16 (~103), plus willjow, Wyatt-James and others. Effectively single-maintainer today.
* Post-v1.61 upstream themes: enhanced/hi-res rendering (512-wide targets, true hi-res Mode 5, 800px wide mode, Mode 7 2×), stereoscopic VRAM sharing, per-game settings granularity (layer toggles, NMI trigger override, per-entry thumbnail dimensions), rendering correctness fixes, NDSP volume/CPU-limit tuning.

## Known issues (from `KNOWN_ISSUES.md`)

1. **Old 3DS performance** — SuperFX and SA-1 titles generally below full speed; heavy in-frame palette-change games (racing roads, gradient skies) also struggle.
2. **In-frame palette changes** — the "Enabled" setting is most accurate (New 3DS default); disabling gains speed but glitches: Top Gear 1/2 roads, Super Turrican gradients, Mortal Kombat 2 portraits, Timon & Pumbaa.
3. **Flickering horizontal line** — thin flickering line in Super Metroid / Yoshi's Island (SMW2); suspected timing issue; games playable.
4. **Audio accuracy** — the core is Snes9x 1.43 with the old APU (not Blargg's), so some games have inaccurate or glitchy audio.
5. **Game-specific table** (~104 entries, from the GBAtemp compatibility list):
   * Hard-broken: Accele Bird, Clay Fighter 2, Dirt Racer, Rocky Rodent, Xardion, Marko's Magic Football, F1 Grand Prix, Star Ocean DeJap hack, among others.
   * Missing peripherals: Super Scope titles (Battle Clash, Yoshi's Safari…), lightgun, Mario Paint (needs joypad patch), Miracle Piano.
   * O3DS-only slowdowns: SuperFX titles (Doom, Star Fox, Stunt Race FX), Super Mario Kart, SPC7110 titles.
   * Graphical: Super Formation Soccer fields, Romancing SaGa 3 / Tokimeki Memorial garbled text, Top Gear 3000.
6. **BS-X/Satellaview** — BIOS must be at `3ds/snes9x/BS-X.bin` (exact casing); SoundLink music is gone (offline); many titles hang on broadcast-wait/time-check screens (workarounds: fast-forward, community patches like d4s BS Excitebike).

`source/problems.txt` is the inherited 2016-era upstream Snes9x developer bug journal — historical reference only, but several of its still-open entries cross-corroborate current known issues (Cu-On-Pa lockup, Madara 2 hi-res clipping, DBZ split-screen flashing).

## Licensing

* **Not OSI open source.** The bundled Snes9x core carries the **Snes9x non-commercial license** (`source/Snes9x/copyright.h`): free redistribution in source/binary **for non-commercial purposes only**, copyright notice must be preserved, provided as-is with no warranty, "freeware for PERSONAL USE only".
* `LICENSE.md` states the combined work is governed by that license; project-specific frontend code is "mixed license — check `THIRD_PARTY_NOTICES.md` and file headers".
* Third-party components (`THIRD_PARTY_NOTICES.md`): Snes9x core, citro3d, libctru, libpng, zlib, libm, devkitPro toolchain, bundled makerom binaries (Project_CTR — provenance in `makerom/BINARY_SOURCES.md`).
* Distribution packages must ship both `LICENSE.md` and `THIRD_PARTY_NOTICES.md` and keep original file headers.

## Emulator compatibility (for development/testing)

* **Citra**: nightly ≤ 2104 works (the app detects emulators via `svcGetSystemInfo` and applies workarounds — no LCD retuning, different cache-flush syscalls, a dummy texture copy to flush the Mode 7 surface).
* **Azahar**: renders the Mode 7 1024×1024 texture as solid yellow (broken).
* `make citra` builds and launches directly.
