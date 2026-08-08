# The Snes9x Emulation Core

[← Back to index](README.md)

Everything under `source/Snes9x/`. Special chips have [their own page](special-chips.md); the GPU rendering path (`gfxhw.cpp`) is detailed in [Rendering Pipeline](rendering.md).

## Provenance

* Base: **Snes9x 1.43** (`snes9x.h:7`). Copyright block: Gary Henderson, Jerremy Koot, John Weidman, Brad Jorsch, Matthew Kendora, Nach, zones, et al.
* Back-ports: `spc700.cpp` from **1.51**; `BSXItself`/`BSXBootup` settings, `CMemory::BIOSROM` and essentially the whole `bsx.cpp` from **1.52**.
* Compiled-out legacy features (defines absent): `CPU_SHUTDOWN`, `SPC700_SHUTDOWN`, `DEBUGGER`, `ZSNES_FX`, `MK_APU`, `CORRECT_VRAM_READS`, `UNZIP_SUPPORT`. Audio is therefore the **old 1.43-era APU**, not Blargg's — the main source of audio inaccuracy (see [Project Meta / Known Issues](project-meta.md)).
* `port.h`: `__3DS__` selects `LSB_FIRST` + `FAST_LSB_WORD_ACCESS`.

## 65c816 CPU (`cpuexec.cpp`, `cpuexec-ops.h`, `getset.h`)

### ARM optimizations (3DS-specific)

* **Global register allocation**: `fastOpcodes` in `r11`, `fastCPUCycles` in `r10`, `fastCPUPC` in `r9` (`cpuexec.cpp:29-46`), with hand-written `stmfd`/`sub sp` prologue in `S9xMainLoop` to work around a GCC bug with global register variables.
* **State consolidation**: `SCPUState OCPU` holds everything — `CPU`, `ICPU`, `Registers`, `OpenBus`, `OpAddress` are all `#define`d into it, plus copies of `MemoryMap`/`MemoryWriteMap`/`MemorySpeed` — to shorten ARM literal-pool addressing.
* `cpuexec-ops.h` has fast private memory accessors (`CpuGetByte`/`CpuSetByte`…) that save/restore the register-cached PC/cycles only around register-space calls.

### Dispatch

* Function-pointer tables selected by M/X/E flags: `S9xOpcodesE1`, `M1X1`, `M1X0`, `M0X1`, `M0X0`.
* Two full table sets exist: `*Original` (fast path) and `*WakeSA1` — store opcodes in the latter compare the write address against `SA1.WaitByteAddress1/2` and wake a sleeping SA-1. Switched per-game by `S9xUseInstructionSet()` from `ApplyROMFixes`.
* The main loop unrolls 20 opcodes per iteration. `S9xMainLoopWithSA1` is a separate copy that interleaves **3 SA-1 opcodes per 65816 opcode**; the frontend picks which loop to call based on `Settings.SA1`.

### Cycles and timing

* Master cycle units: `ONE_CYCLE=6`, `SLOW_ONE_CYCLE=8`; `Settings.H_Max ≈ 1364` master cycles/scanline; 262 scanlines NTSC / 312 PAL.
* Flat `CPU.MemSpeed` charge at opcode dispatch + per-access `CPU.MemorySpeed[block]` charges; addressing modes add explicit extra costs.
* FastROM: `$420D` toggles `CPU.FastROMSpeed` between 6 and 8, applied to the upper banks by `FixROMSpeed()`.

### Interrupts

* `CPU.Flags`-driven, checked after every opcode. NMI is delayed by `NMICycleCount` (trigger point defaults to 4; per-game overrides 3/25, plus a per-game UI override). IRQ delayed by `IRQCycleCount` (default 3; 0 for Power Rangers Fight).
* SA-1 can override the NMI/IRQ vectors via `$220C-$220F`.
* IRQ sources: H-beam, V-beam, SuperFX (GSU), SA-1 (+DMA).

### Scanline event machine

`S9xDoHBlankProcessing()` (`cpuexec.cpp:371`), driven by `CPU.NextEvent`:

* **HBLANK_START** → `S9xDoHDMA()`.
* **HBLANK_END** → SuperFX slice, `S9xUpdateAPUTimer()`, the **deferred `$212C-$2131` register optimization** (writes latched during the line; flush only if the end-of-line value differs), cycle rebase, `V_Counter++`, `S9xStartHDMA()` on wrap, V-timer IRQ, palette commit line, V-blank start (NMI arm, `$4210`), joypad update, `RenderLine()`.
* **HTIMER_BEFORE/AFTER** → H-beam IRQ.

### Speed hacks

`ApplySpeedHackPatches()` patches known idle-loop opcodes to `0x42` (WDM). The `Op42` handler executes the saved original opcode, then — if no interrupt is imminent — jumps `CPU_Cycles` to the next event. Registered per-game in `ApplyROMFixes` (Yoshi's Island, Super Mario Kart, F-Zero, Axelay, Ace o Nerae, plus a long SA-1 list). Re-applied on the first frame after every ROM load.

## APU (`spc700.cpp`, `apu.cpp`, `soundux.cpp`)

* **SPC700**: function-pointer opcode table (`S9xApuOpcodes[256]`); cycle table scaled by `IAPU.OneCycle = 21`. Registers aliased into `APU.FastRegisters` (same literal-pool trick as the CPU).
* **Execution**: driven per-scanline by `S9xUpdateAPUTimer()`, running SPC opcodes 10-at-a-time to catch up with `CPU.Cycles`; timers 0/1 at 8 kHz, timer 2 at 64 kHz. A 3DS-specific `/10` in the timer position math avoids software 64-bit division (unsupported on devkitARM).
* **Deferred DSP write queue** (3DS-specific, cross-core safety): SPC writes to `$F3` go into a 1024-entry ring (`IAPU.DSPWriteBuffer`) mirrored immediately to `IAPU.DSPCopy[]` for read-back; the audio thread drains the ring via `S9xSetAPUDSPReplay()` before mixing. Reads of `OUTX`/`ENVX`/`ENDX` have special handling (Terranigma, Clock Tower fixes).
* **Mixing** (`soundux.cpp`): per-channel routines specialized into 16 variants keyed by echo/envelope/pitch-mod flags (`MixComputeFuncPtr[16]`); BRR block decode, noise LFSR, echo buffer with 8-tap FIR filter. Three-stage platform handoff: `S9xMixSamplesIntoTempBuffer` → optional `S9xGenerateSilenceIntoTempBuffer` → `S9xApplyMasterVolumeOnTempBufferIntoLeftRightBuffers` (de-interleaved 16-bit L/R). See [Audio and Timing](audio-and-timing.md).

## PPU (`ppu.cpp`, `gfx.cpp`, `gfxhw.cpp`, `tile.cpp`, `ppuvsect.cpp`, `cliphw.cpp`)

| File | Status |
|---|---|
| `ppu.cpp` | **Live** — all PPU/CPU register decode (`S9xSetPPU/GetPPU/SetCPU/GetCPU`), reset, joypads, SuperFX driver |
| `ppu.h` | `SPPU`/`InternalPPU` structs, register-write inlines, `FLUSH_REDRAW`, `LayerRenderState` |
| `gfx.cpp` | **Partly dead** — live: palettes, `RenderLine`, screen refresh start/end, `S9xSetupOBJ`, direct-color maps; dead: the entire software rasterizer (`RenderScreen`, `DrawBackground*`, `DrawOBJS`…). `S9xUpdateScreenSoftware` is declared but never defined |
| `gfxhw.cpp` | **The live 3DS hardware renderer** (see [Rendering Pipeline](rendering.md)) |
| `tile.cpp` | `ConvertTile` (VRAM bitplanes → 8-bit tile cache) is live; the software blitters are dead |
| `ppuvsect.cpp` | Vertical section tracking (below) |
| `cliphw.cpp` | Window → stencil section computation |
| `hwregisters.cpp` | `MAP_*` enum → chip handler dispatch |

Key mechanisms:

* **Register-write inlines** (`REGISTER_2104/2118/2119/2122/2180` in `ppu.h`) do change detection: VRAM writes only invalidate tile caches when a byte actually changed; OAM writes decode directly into `PPU.OBJ[]`.
* **`FLUSH_REDRAW()`** is the render trigger: any mid-frame change to a rendering-relevant PPU register flushes the screen section rendered so far via `S9xUpdateScreenHardware()`.
* **Vertical sections** (`ppuvsect`): brightness, backdrop color, fixed color and window L/R positions are tracked as per-frame lists of `[StartY, EndY, value]` so in-frame changes don't force full redraws. Four instances live in `IPPU`.
* **In-frame palette changes (HDMA)**: `REGISTER_2122` tracks which palette groups HDMA touched (`HDMAPalette16Mask`, `HDMAPalette4BGMask`); `gfxhw` keeps a 4096-entry **HDMA palette-variant texture cache** so one tile can appear with several mid-frame palettes in a single frame. A per-layer deferral gate (`LayerRenderState`) lets palette-only writes skip re-rendering layers that don't sample the changed palette window (per-game override: `settings3DS.PaletteDeferBgMask`). `SNESGameFixes.PaletteCommitLine` selects the strategy (−1 default / −2 deferred-layer mode / 0-240 fixed commit line) — surfaced in the UI as "In-Frame Palette Changes", with per-title heuristic defaults.

## Memory map (`memmap.cpp`)

* 4096 × 4 KB blocks: parallel `Map[]`, `WriteMap[]`, `MemorySpeed[]`, `BlockIsRAM[]`, `BlockIsROM[]`. Pointers below `MAP_LAST` are enums dispatched to chip handlers (`hwregisters.cpp`); above are direct host pointers. Word reads split at block boundaries.
* **ROM loading** (`LoadROM` → `FileLoader` → `InitROM`):
  * Copier-header detection (file size mod 8 KB == 512), split multi-file ROMs (`.1`/`.2`…, `sfNNNNa/b/c`), ZIP/RAR rejected.
  * LoROM/HiROM decided by **content scoring** (reset vector, checksum complement pairing, map-mode byte, ASCII title).
  * Interleave detection + deinterleavers (type 1, type 2, GD24, Tales-style extended), with retry on contradiction; extended (>4 MB) formats (`BIGFIRST`/`SMALLFIRST`).
* **Chip detection** from header `(ROMType, ROMSpeed)` combinations → mapper selection: `HiROMMap`, `LoROMMap`, `SA1ROMMap`, `SuperFXROMMap`, `SPC7110HiROMMap`, `TalesROMMap`, `SetaDSPMap`, `JumboLoROMMap`, `AlphaROMMap`, BS-X dynamic mapping, plus special-case maps for a handful of titles (24 Mbit SoundNovel, 512K-SRAM, Sufami Turbo).
* SRAM: `SRAMMask` from header size; LoROM SRAM at `$70-$7D`/`$F0-$FD`, HiROM at `$6000` windows; initial fill value per-game (`SNESGameFixes.SRAMInitialValue`).
* `ApplyROMFixes()`: per-title hacks database — NMI trigger points, IRQ cycle counts, SRAM initial values, speed-hack tables, SA-1 wake addresses, instruction-set selection.
* Fork fix: `ResetSpeedMap()` forces `CPU.FastROMSpeed = SLOW_ONE_CYCLE` on ROM load so FastROM state doesn't leak between games.

## DMA / HDMA (`dma.cpp`)

* `S9xDoDMA` handles the 8 general channels with special paths: S-DD1 pre-decompression into a 64 KB buffer, SPC7110 decompressed reads, SA-1 character-conversion DMA, WRAM→WRAM skip, forced `FLUSH_REDRAW` on `$2118/$2119` targets (Mickey & Donald 3 fix).
* `S9xStartHDMA`/`S9xDoHDMA` implement the per-scanline HDMA state machine: line count/repeat decode, indirect addressing, 8 transfer modes with per-mode cycle charges, and per-game workarounds (Hook channel termination, Uniracers OAM address fix).

## Key global structs

All instantiated in `globals.cpp`:

| Struct | Contents |
|---|---|
| `SSettings Settings` | Core-wide switches: timing, ROM overrides, chip enables, sound config, graphics options, per-game hack flags, `HWOBJRenderingMode` (3DS addition) |
| `SCPUState OCPU` (= `CPU`) | Execution state, flags, cycle counters, event machine, SRAM autosave timers, plus the 3DS fast-state block |
| `SPPU PPU` | All architectural PPU registers: BG config, CGRAM, OAM, Mode 7 matrix, windows, IRQ positions |
| `InternalPPU IPPU` | Emulator-internal PPU state: dirty flags, tile caches, render cursors, plus 3DS additions (Mode 7 dirty tracking, vertical sections, HDMA palette masks, deferred register writes) |
| `CMemory Memory` | Memory map + ROM metadata (name, ID, checksums, CRC32, region, format) |
| `SSNESGameFixes SNESGameFixes` | Per-game hacks; fork additions: `PaletteCommitLine`, `IRQCycleCount`, speed-hack tables |
