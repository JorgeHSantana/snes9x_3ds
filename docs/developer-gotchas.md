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
* **`EXTRA_DEFINES` is not tracked by the Makefile.** Objects built with one probe set stay "up to date" under another: remove `build/{3dsimpl,dma,gfxhw,ppu,gfx}.o` (or `make clean`) when the defines change, or the probe silently stays out (two measurement rounds lost on 2026-09-26).
* **picasso: `add r0.y, uniform.w, r0.y` does not do what it says.** A swizzled uniform as src1 of `add` with a masked destination shifted the whole picture (4 px at the edges, measured); `mov r7, uniform` then `add r0.y, r7.w, r0.y` is exact. Prefer a register copy when reading one component of a uniform.
* **Stencil is not a tool here.** Bits 0-4 are free (windows use 5-7) and real stencil ops need the depth stage enabled, but with the texture-backed depth buffer Azahar's results changed from build to build and the console flickered (drop shadows v2, 2026-09-27). The game texture's alpha as a mask (v3) was heavy and wrong on the console too.
* **The depth buffer DOES hold the visible surface's plane per pixel** - until colour math. Sprites write `(prio+1)*3` with the test off, BG tiles their mode's d0/d1 under GEQUAL, transparent/windowed pixels never write; the colour-math and brightness rects then write z = 0 over the whole screen. A measurement taken after them ("GEQUAL and LEQUAL pass everywhere, GREATER and LESS nowhere", 2026-09-27) read a wiped buffer and sent two shadow versions the wrong way. Anything that wants "where is row R the surface" is one draw with `C3D_DepthMap(true, 0, plane/32)` + depth func EQUAL, before colour math (drop shadows v4).
* **`LINE_MAX` is a devkitARM macro** (limits.h): a class constant with that name fails to compile with a confusing "expected unqualified-id".
* **Never open a file for writing in the same expression that reads it** (`open(p,'w').write(open(p).read()...)` truncates first): the CHANGELOG went to a commit empty that way. Read into a variable, then write.

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
* **Packed render state bit-fields are byte-sized enums** (devkitARM short enums): a field must not straddle a byte boundary, or the union silently grows past 64 bits and the `_Static_assert` fires. When a field width changes, re-lay the struct and the `OFF_*` macros together so every byte's fields add up to at most 8 (see the comment above `OFF_STENCIL`). A new render target needs the same index in `SGPU_TARGET_ID` and `SGPU_TEXTURE_ID` (the apply reads `textures[(SGPU_TEXTURE_ID)target]`).
* **VRAM is nearly full.** After the SNES and UI textures only ~32 KB were left (2026-10-15): a mandatory 32 KB target made `C3D_TexInitVRAM` fail on the second screen's background and the emulator exit at boot. Allocate anything new last, optional, and log it (`impl3dsAllocGlowTexture` is the pattern).
* **`gpu3dsDraw` applies the render state again.** A pass that overrides a state field directly through C3D (alpha test, depth) must leave the applied copy of that field as the draw expects, or the draw's own apply puts the default back over the override (the glow's luminance threshold did nothing until the alpha-test poison was removed).
* **The game texture reaches the TexEnv with R and B swapped** relative to the screen. Luminance weights are per operand (`SCREEN_GRADE_LUMA_*_OP_WEIGHT`: 29 on SRC_R, 76 on SRC_B), measured texel by texel on Azahar.
