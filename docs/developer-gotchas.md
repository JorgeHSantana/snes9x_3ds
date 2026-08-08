# Developer Gotchas

[← Back to index](README.md)

Cross-cutting invariants and traps to know before changing code.

## Ordering and concurrency invariants

* **`gpu3dsPrepareSnesScreenForNextFrame()` must run before `S9xMainLoop()`** — it flips the double-buffered VBO halves; reordering aliases CPU vertex writes with in-flight GPU reads.
* **All audio-adjacent teardown must be fenced** with `snd3dsDrainMixing()` / `snd3dsResumeMixing()` — the mixing thread reads live APU state on another core. Fenced paths today: ROM load, reset, savestate save/load, screenshots, menu entry.
* `gpu3dsSetTopMode()` must render a frame **before** `lcd3dsSetEmulationRate()` when entering emulation, or the top screen glitches / hardware freezes.
* LCD rate restoration must recompute the top-screen `VTotal` from the **current** 2D/3D mode — restoring a stale 3D value while in 2D leaves the panel in a broken timing state.
* Screenshot capture blocks on `GSPGPU_EVENT_PPF` — it hangs forever if no frame was actually rendered first.

## Hardware quirks

* Mixing `DrawArrays` and `DrawElements` within the BG/OBJ pass **freezes real 3DS hardware** (observed in Super Mario Kart) — the renderer chooses all-or-nothing per frame (`useDrawArraysForTiledLayers`).
* PICA can't render to viewports wider than 512 px — the 1024×1024 Mode 7 texture is baked in 4× 512×512 sections with the color buffer pointer repointed per section.
* The right-eye render target **aliases** the second half of the left target's wide-mode buffer — wide mode and stereo 3D must remain mutually exclusive (`gpu3dsGetTopMode()` enforces this structurally).

## Data-structure invariants

* The tile-cache hash region must keep an **even** slot count — alt-frame slots are `p^1` pairs (mid-frame tile rewrite support, e.g. DKC2).
* The HDMA palette-variant pool (4096 slots reserved at the top of the atlas) must stay ≥ its current size relative to the hash region.
* Layer draw order (`LAYER_BG0..WINDOW_LR` enum order) and the per-frame `drawOrder[]` are load-bearing — window stencil must be laid down before SUB/MAIN passes.
* Mode 7 dirty-tile culling happens **in the vertex shader** (`Position.w` stamp vs the `updateFrame` uniform). CPU-side you only stamp vertices and mark sections; all ~16 K vertices are submitted every bake regardless.
* `DirectoryEntry` (directory cache) is written verbatim with `fwrite` — it must remain POD with fixed-size fields.
* `g_fileBuffer` / `g_streamBuffer` / `g_texUploadBuffer` are shared, single-owner buffers — never use concurrently from two call sites.

## Build traps

* A bare `make` builds **only the citro3d dependency** (default goal); use `make release` or a named target.
* New `.cpp` files must be added to the Makefile's explicit `CPPFILES` list — nothing is globbed.
* The `release` target skips the patched-citro3d dependency — CI links the container's stock citro3d.
* `make clean` deletes the tracked `romfs/gfx/splash.t3x`.
* `TARGET := $(notdir $(CURDIR))` — the output binary is named after the checkout directory.
* Profiling (`3dstimer`) compiles to nothing unless `PROFILING_DISABLED` is undefined in `3dstimer.h`; re-enable the define before release builds.

## Emulator-vs-hardware differences

Behavior keys off `GPU3DS.isReal3DS` (detected via `svcGetSystemInfo(0x20000, 0)`):

* LCD `VTotal` retuning is skipped on emulators.
* Cache flushing uses `svcFlushProcessDataCache` on hardware vs `DSP_FlushDataCache` on emulators (Citra lacks the SVC).
* Citra needs a dummy 16-byte `C3D_SyncTextureCopy` to flush the baked Mode 7 surface.
* The audio thread runs on core 1 on emulators regardless of model.
* Citra nightly ≤ 2104 works; Azahar renders Mode 7 as solid yellow.

## Dead code and misleading declarations

* The entire software rasterizer in `gfx.cpp` (`RenderScreen`, `DrawBackground*`, `DrawOBJS`, Mode 7 software paths) and the blitters in `tile.cpp` are dead — only `gfxhw.cpp` renders. `S9xUpdateScreenSoftware` is declared but never defined.
* `settings3dsApplyScreenLayout()` is declared in `3dssettings.h` but never defined — the real implementation is `ui3dsSetScreenLayout()` in `3dsui.cpp`.
* Legacy compile-time features (`CPU_SHUTDOWN`, `SPC700_SHUTDOWN`, `DEBUGGER`, `ZSNES_FX`, `UNZIP_SUPPORT`…) are never defined — their code paths are dead.
* `source/problems.txt` is a 2016-era upstream bug journal, not a build input.

## Behavioral fine print

* Frame skipping skips **rendering only**; emulation always runs every frame (`IPPU.RenderThisFrame`).
* Savestates do **not** capture SuperFX/C4/S-DD1/DSP/BS-X/OBC1/SETA chip state — be careful assuming round-trip fidelity for those games.
* Cheat application routes register-space writes through `S9xSetByte` deliberately (timing safety) — don't "optimize" it to direct writes.
* Speed-hack patches are re-applied on the first frame after every ROM load; they patch ROM bytes in place to opcode `0x42`.
* Asset matching is by **trimmed ROM name** (+ `mappings.txt` aliases), not checksum — renaming a ROM breaks its thumbnails/cheats/config associations.
