# The 3DS Platform Layer

[← Back to index](README.md)

All files matching `source/3ds*.{cpp,h}` plus `png_utils.*` and `bufferedfilewriter.h`. The GPU-specific modules (`3dsgpu`, `3dsimpl_gpu`, `3dsimpl_tilecache`, shaders) are covered in [Rendering Pipeline](rendering.md); audio (`3dssound`) and timing (`3dslcd`) in [Audio and Timing](audio-and-timing.md); menus (`3dsmenu`, `3dsui*`, `3dsthemes`, `3dsfont`) in [Menu and UI](menu-ui.md).

## `3dsmain.cpp` — application shell

* `main()` (`3dsmain.cpp:2268`): New3DS detection, `osSetSpeedupEnable(true)`, settings load, then `emulatorInitialize()` (`:2016`) which brings up subsystems in strict order: log → gfx (both screens BGR8) → UI splash message → romfs (+`mappings.txt`) → files → GPU → UI → notifications → impl → images → sound → APT hooks.
* State machine on `GPU3DS.emulatorState` (see [Architecture](architecture.md)). Exit path saves settings, auto-savestate and cheats, then tears down in reverse order.
* `emulatorLoop()` (`:2152`) runs the per-frame sequence: input scan → `impl3dsRunOneFrame()` → tick measurement → `paceFrame()`. The first frame after menu skips drawing for responsiveness. On entry it renders one frame via `gpu3dsSetTopMode()` **before** `lcd3dsSetEmulationRate()` — otherwise the top screen glitches or the hardware freezes.
* All menus are built here (`makeEmulatorMenu` `:287`, `makeOptionMenu` `:761`, `makeControlsMenu` `:1007`, `makeCheatMenu` `:1146`); `setupMenu()` (`:1679`) rebuilds only tabs flagged dirty in `settings3DS.menuTabDirty[]`, preserving selection. 2 tabs with no ROM loaded (Emulator, Load Game), 5 in game.
* `emulatorLoadRom()` (`:1479`): saves previous game's state/config/cheats, drains the audio mixer, clears `Memory.ROMCRC32`, loads the ROM, clears SNES textures, resets per-game defaults, re-reads per-game config, resets conflicting hotkeys, optional auto-load of savestate.
* The file browser keeps a `fileMenuScrollStack` preserving scroll position per directory depth; X opens a context menu (set/reset default dir, random game, rescan, delete game).
* Many containers are static/global deliberately, to avoid heap fragmentation on the 3DS heap.

## `3dsimpl.cpp` — the bridge

Implements both the `impl3ds*` API consumed by `3dsmain` and the `S9x*` callbacks required by the core.

* `impl3dsInitialize()` (`:151`): loads the 3 shader programs (geometry strides 0/6/3), allocates VRAM textures and linear-RAM tile caches, creates the VBOs, then configures and initializes the core (`Memory.Init()`, `S9xInitAPU()`, `S9xGraphicsInit()`, `S9xInitSound()`; `Settings.H_Max = SNES_CYCLES_PER_SCANLINE`, 32 kHz stereo, `AutoSaveDelay = 3600`).
* `impl3dsRunOneFrame()` (`:869`) — the per-frame heartbeat (see [Architecture](architecture.md)).
* `impl3dsSceneRender()` (`:759`): computes the `GameScreenViewport` from stretch/crop/overscan settings — half-texel inset (`tx0 = 0.5`) to avoid a linear-filter edge line; crop quantization when stretched so top/bottom rounding stays symmetric; special-casing for 239/240-line games. Then per eye: background image → SNES_MAIN quad → optional "Balanced" second filter pass (linear + 50% vertex alpha) → 1px black bar for 239-line games → scanlines → bezel overlay → notifications. Stereo renders twice with ±IOD offsets (`iod = slider × base`, base 3/5/8 px for Standard/Medium/High intensity).
* snes9x callbacks: `S9xReadJoypad` (`:1426` — pad 0 only; circle-pad-as-dpad binding; 4 mapping slots OR'd per 3DS button; turbo via per-button alternating frame masks), `S9xOpenSnapshotFile`/`S9xCloseSnapshotFile`, `S9xGetFilenameInc`, `_splitpath`/`_makepath`, `S9xMessage`, mouse/Super Scope stubs (return FALSE — no peripheral support).
* `impl3dsQuickSaveLoad()` (`:1019`): drains the mixer, shows an in-progress notification and renders one frame before the blocking save, then shows the result.
* **Broken-audio savestate detection**: `impl3dsHasBrokenAudioStateSignature()` (`:527`) heuristically detects states saved with dead audio (SPC stuck in IPL ROM, DSP FLG = mute|echo-off, no keyed channels, other DSP regs zero) and warns before loading; context logged to `<state>.broken-audio.log`.
* `impl3dsTakeScreenshot()` (`:1135`): waits for the display transfer (`GSPGPU_EVENT_PPF`), **undoes** the frame-end buffer swap, reads back the framebuffer and saves a PNG. Savestate screenshots are captured at 0.5 scale.
* `S9xAutoSaveSRAM()` (`:1274`): sets the mixer's `generateSilence` flag (instead of stopping NDSP) while writing `.srm`.

## Other platform modules

| Module | Role |
|---|---|
| `3dsinput` | Single `hidScanInput()` per frame (`input3dsScanInputForEmulation` `:63`); hotkey dispatch via `ButtonMapping::IsHeld` (chords supported); menu open (also on touch — auto-saves SRAM first), controller swap, quick save/load, slot ±, screenshot, fast-forward toggle/hold; input latch (`input3dsWaitForRelease`) when returning from menu so the A press doesn't leak into the game |
| `3dsfiles` | ROM browsing. **Directory cache**: binary POD records in `.dir_cache/` (magic `SNIX`, versioned), used for directories ≥50 entries, corrupt caches auto-deleted. Shared I/O buffers: 512 KB `g_fileBuffer` + 32 KB cache-aligned `g_streamBuffer` handed to one open file at a time via `setvbuf`. `file3dsGetRelatedPath()` (`:517`) derives all per-ROM paths, optionally trimming region tags and applying `mappings.txt` aliases |
| `3dssettings` | `S9xSettings3DS settings3DS` global; global vs per-game split with `UseGlobal*` switches; `settings3dsUpdate()` recomputes derived values (`TicksPerFrame`, palette-fix mode → `SNESGameFixes.PaletteCommitLine`, SRAM autosave delay); per-title heuristic default for the palette fix on Old 3DS |
| `3dsconfig` | Versioned INI-ish `key=value` read/write (global file v1.6, game file v1.5); the file starts with `# v<x.y>` so newer keys are skipped when reading older files; primitives for int (with clamping), string, bitmask and enums |
| `3dsexit` | APT hooks (`handleAptHook`): HOME/sleep → restore CPU limit, stop audio (prevents a hung looped sample), restore LCD rate, conditionally autosave SRAM, drop to pause menu; resume → reapply CPU limit, mark screens dirty |
| `3dslog` | Session log `debug_v<ver>_session.log` (truncated each run), gated on `LogFileEnabled`; `[SS.mmm]` elapsed-time prefixes; `fflush` after every write |
| `3dstimer` | Profiling buckets (main loop, SuperFX, draws, GPU wait…), compiled out unless `PROFILING_DISABLED` is undefined; toggled in-game with SELECT+L+Right/Left; 120-frame window |
| `3dsutils` | DJB2 string hash (thumbnail cache keys), sanitized paths, trimmed basenames, RNG helpers |
| `png_utils` | libpng decode — everything normalized to 8-bit RGBA into `g_fileBuffer`, size-capped; fast encode with compression level 1; RAII handles |
| `bufferedfilewriter.h` | Write-buffering RAII wrapper over the shared `g_fileBuffer`; flushes on overflow, bypasses for >512 KB writes; used by config and savestate writers |

## Settings object highlights (`3dssettings.h`)

* Enums under namespace `Setting`: `ScreenFilter{Sharp,Smooth,Balanced}`, `ScreenStretch{None, 4:3 (298px), CRT (292px), Fit 4:3, Fit 8:7, Full}`, `AssetMode{None,Default,Adaptive,CustomOnly}`, `Theme`, `Font`, `Framerate{UseRomRegion,ForceFps60}`, `FrameSync{VBlank,Sleep}`, `Intensity3D`, `EnhancedResolution{Off,Standard,Wide}`.
* `ButtonMapping<N>` template: arrays of held-bitmasks where `IsHeld()` requires the full mask (chord support).
* Runtime-only fields (never persisted): `RootDir`, screen dims, `TicksPerFrame`, `TurboMode`, `LayerEnabled[8]`, dirty flags.
* Note: `settings3dsApplyScreenLayout()` is declared in `3dssettings.h` but never defined — the real implementation is `ui3dsSetScreenLayout()` in `3dsui.cpp`.
