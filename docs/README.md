# Snes9x for 3DS — Documentation

Technical context documentation for the **snes9x_3ds** repository (the standard/2D-focused fork).
Generated 2026-08-08 from source analysis of the codebase at version **v1.61** (+17 commits).

## Table of Contents

| Document | Contents |
|---|---|
| [Overview](overview.md) | What the project is, fork lineage, key characteristics, repository layout |
| [Build System](build-system.md) | Toolchain, Makefile, flags, shaders/assets, targets, CIA packaging |
| [Architecture](architecture.md) | The two halves (platform layer ↔ core), the bridge, per-frame control flow |
| [Platform Layer](platform-layer.md) | `3dsmain`, `3dsimpl`, `3dsgpu`, input, files, config, and the other `3ds*` modules |
| [Emulation Core](emulation-core.md) | Snes9x 1.43 provenance, 65c816 CPU, APU/SPC700, PPU, memory map, DMA/HDMA |
| [Rendering Pipeline](rendering.md) | GPU-accelerated PPU: tile cache, shaders, layer batching, Mode 7, composition |
| [Audio and Timing](audio-and-timing.md) | NDSP pipeline, mixing thread, LCD refresh retuning, frame pacing |
| [Settings and Storage](settings-and-storage.md) | Global vs per-game settings, config file format, SD card layout |
| [Save States and Cheats](saves-and-cheats.md) | Savestate format, SRAM autosave, screenshots, cheat formats and engine |
| [Special Chips](special-chips.md) | SuperFX, SA-1, DSP-1..4, C4, S-DD1, SPC7110, OBC1, SETA, S-RTC, BS-X |
| [Menu and UI](menu-ui.md) | Second-screen menu system, themes, thumbnails, notifications, splash |
| [MSU-1 Support](msu1.md) | Usage, file naming, wave-1 limitations, hardware validation checklist |
| [Project Meta](project-meta.md) | CI workflows, release process, contribution policy, known issues, licensing |
| [Developer Gotchas](developer-gotchas.md) | Cross-cutting invariants and traps to know before changing code |
| [**Code Audit**](code-audit.md) | Verified source-level defects, duplicated code and dead code, with fixes |

## Quick Facts

* **What**: SNES emulator for Nintendo 3DS/2DS (all models); the SNES PPU runs on the 3DS GPU (PICA200).
* **Lineage**: Snes9x 1.43 core → bubble2k16/snes9x_3ds → matbo87/snes9x_3ds (upstream) → this fork.
* **Current version**: v1.61 (app metadata `1.61.0`, product code `CTR-P-SNSX`).
* **Language/toolchain**: C++17 (gnu++17), devkitARM, libctru, patched citro3d v1.7.1.
* **ROM formats**: `.smc`, `.sfc`, `.fig`, `.bs`, `.bsx` — no ZIP support.
* **Data root on console**: `sd:/3ds/snes9x_3ds/`.
* **Assets** (thumbnails, cheats): separate repo [matbo87/snes9x_3ds-assets](https://github.com/matbo87/snes9x_3ds-assets), No-Intro naming, matched by ROM name.
* **License**: Snes9x non-commercial license — not OSI open source.
