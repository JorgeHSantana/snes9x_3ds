# Overview

[← Back to index](README.md)

**Snes9x for 3DS** is a Super Nintendo (SNES) emulator for the Nintendo 3DS/2DS family. It is a fork lineage:

```
Snes9x 1.43 (PC, 2004-era core, with selective 1.51/1.52 back-ports)
   └─ bubble2k16/snes9x_3ds   (original 3DS port, hardware-accelerated PPU)
        └─ matbo87/snes9x_3ds (current upstream — modernized architecture, citro3d, NDSP)
             └─ this checkout (origin = JorgeHSantana/snes9x_3ds, personal fork)
```

## Key characteristics

These are what distinguish it from a vanilla Snes9x port:

* **The SNES PPU is emulated on the 3DS GPU (PICA200)**, not in software. SNES tiles are decoded into texture atlases and drawn as batched quads with depth/stencil tricks reproducing SNES priorities, windows and color math. This is what makes full-speed emulation possible on the Old 3DS's 268 MHz ARM11. See [Rendering Pipeline](rendering.md).
* **The CPU core is heavily ARM-optimized**: global register variables pinned to ARM registers (`r9`–`r11`), state consolidated into single structs to shorten literal-pool addressing, per-game speed-hack opcode patching. See [Emulation Core](emulation-core.md).
* **Audio runs on a separate core** via a mixing thread feeding NDSP (migrated from CSND in v1.61). See [Audio and Timing](audio-and-timing.md).
* **The physical LCD refresh rate is retuned** to the exact SNES rate (60.0988 Hz NTSC / 50.007 Hz PAL) by writing the PDC `VTotal` hardware registers.
* Rich frontend: themes, per-game configs, thumbnail packs, backgrounds/overlays, save-state screenshots, cheats, hotkeys, stereoscopic 3D splash/backgrounds. See [Menu and UI](menu-ui.md).

Works on all 3DS/2DS models. Old 3DS struggles mainly with SuperFX / SA-1 titles. A modded console is required; DSP firmware (`3ds/dspfirm.cdc`) is needed for sound.

Supported ROM formats: `.smc`, `.sfc`, `.fig`, `.bs`, `.bsx` (no ZIP). Data lives in `sd:/3ds/snes9x_3ds` (see [Settings and Storage](settings-and-storage.md)).

Companion asset repository: **matbo87/snes9x_3ds-assets** (thumbnail packs, cheats; 1G1R selection, strict No-Intro naming — matching is name-based).

## Repository layout

```
snes9x_3ds/
├── Makefile                 # devkitPro two-pass makefile (based on TricksterGuy/3ds-template)
├── source/
│   ├── 3ds*.cpp/.h          # 3DS platform layer (frontend, GPU, audio, UI, settings…)
│   ├── shader_*.pica        # PICA200 shaders (picasso assembly)
│   ├── png_utils.*          # libpng decode/encode helpers
│   ├── bufferedfilewriter.h # RAII buffered writer over the shared I/O buffer
│   ├── problems.txt         # legacy upstream Snes9x 1.43 dev bug journal (historical)
│   └── Snes9x/              # the emulation core (Snes9x 1.43 + backports + 3DS mods)
├── gfx/                     # tex3ds sources (splash screen atlas: splash.t3s + PNGs)
├── romfs/                   # runtime read-only FS embedded into 3dsx/CIA
│   ├── mappings.txt         # ROM-name → canonical-name aliases (asset matching)
│   └── gfx/                 # default overlay/backgrounds PNGs + compiled splash.t3x
├── resources/               # packaging: AppInfo, app.rsf, banner.bnr, icon.png
├── makerom/                 # bundled prebuilt makerom binaries (4 host platforms)
├── patches/                 # citro3d-uniforms-maxdirty.patch (perf patch for citro3d v1.7.1)
├── screenshots/             # README screenshots
├── .github/workflows/       # ci.yml, master-build.yml, pr-base-policy.yml
├── CHANGELOG.md             # project history (current: v1.61)
├── KNOWN_ISSUES.md          # categorized compatibility list (~104 games + BS-X section)
├── LICENSE.md               # Snes9x non-commercial license governs the combined work
└── THIRD_PARTY_NOTICES.md   # bundled/linked third-party components
```

App metadata (`resources/AppInfo`): title *Snes9x for 3DS*, author *bubble2k16, matbo87*, product code `CTR-P-SNSX`, unique ID `0x3849`, version `1.61.0`.

## Project evolution (CHANGELOG highlights)

* **v1.40–1.45** (bubble2k16 era + revival): buffered file writer, screen swap, per-ROM folders, PNG screenshots, BlargSNES DSP core replaced by dsp1-4.
* **v1.5x** (UX/frontend era): dark mode + RetroArch theme, file-menu overhaul, pause screen, game previews (boxart/title/gameplay), improved cheat menu, folder restructure to `3ds/snes9x_3ds`.
* **v1.60** (the big architectural release): rendering migrated to **citro3d** with XOR-based packed render-state diffing and draw-call batching; thumbnails rewritten to on-demand `.cache` packs; SNES-accurate LCD refresh matching; OSD (bezel overlay, FPS, notifications); stereoscopic 3D splash; `-Werror` enforced.
* **v1.61** (current): **CSND → NDSP audio migration**, save-state screenshot previews, per-game crop/overscan, scanlines, Mode 7 bilinear smoothing, Frame Sync setting, audio buffer size setting, HDMA/in-frame palette fixes, mosaic rendering, broken-audio savestate detection, Citra compatibility.
* **Post-v1.61** (in progress on upstream `enhanced-resolution` branch): 512-wide render targets, true hi-res Mode 5, 800px wide mode, Mode 7 2×, per-game layer toggles.
