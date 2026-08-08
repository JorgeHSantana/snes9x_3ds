# Special Chips

[← Back to index](README.md)

Cartridge coprocessors emulated in `source/Snes9x/`. Detection happens in `CMemory::InitROM` from header `(ROMType, ROMSpeed)` combinations (see [Emulation Core](emulation-core.md)); each chip gets its own memory-map enum (`MAP_*`) dispatched via `hwregisters.cpp`.

| Chip | Files | Example games | Notes |
|---|---|---|---|
| **SuperFX (GSU)** | `fxemu.cpp`, `fxinst.cpp`, `fxdbg.cpp` | Star Fox, Yoshi's Island, Doom | C implementation. Driven per-scanline by `S9xSuperFXExec()` (requires GO flag + `SCMR` bits); instruction budget scaled by `speedPerLine`; Winter Gold gets a reduced budget. Plot/rpix opcodes are re-patched per screen mode. GSU state is **not** saved in savestates. Heavy on Old 3DS. |
| **SA-1** | `sa1.cpp`, `sa1cpu.cpp`, `sa1cpuops.cpp` + headers | Super Mario RPG, Kirby Super Star, Kirby's Dream Land 3 | A full second 65816 at 10.74 MHz: `sa1cpu.cpp` re-includes the shared opcode bodies with `CPU`/`Registers` redefined to SA-1 state and cycle accounting disabled. **3 SA-1 opcodes interleaved per main-CPU opcode** (`S9xMainLoopWithSA1`); wake-on-write via the patched `*WakeSA1` opcode tables watching `WaitByteAddress1/2`; own 4096-entry memory map; multiply/divide unit; character-conversion DMA; can override main-CPU NMI/IRQ vectors. Heavy on Old 3DS. |
| **DSP-1/2/3/4** | `dsp.cpp` (dispatch), `dsp1.cpp`…`dsp4.cpp` | Super Mario Kart / Pilotwings (DSP-1), Dungeon Master (DSP-2), SD Gundam GX (DSP-3), Top Gear 3000 (DSP-4) | Function-pointer dispatch (`GetDSP`/`SetDSP`) selected by header. These replaced the BlargSNES DSP core in v1.45. DSP-3 includes a Huffman/LZ decoder and pathfinder arrays; DSP-4 is a road/sprite projection engine. |
| **C4** | `c4.cpp`, `c4emu.cpp` | Mega Man X2/X3 | Wireframe/sprite math chip: float transforms + sin/cos tables (`c4.cpp`) and the register/command engine (`c4emu.cpp`) mapped at `$6000-$7FFF` (`MAP_C4`). |
| **S-DD1** | `sdd1.cpp`, `sdd1emu.cpp` | Star Ocean, Street Fighter Alpha 2 | Arithmetic-coding decompressor invoked from DMA (`S9xDoDMA` pre-decompresses into a 64 KB buffer when `$4801 > 0`). Logged-data support for pre-decompressed packs (`Settings.SDD1Pack`). |
| **SPC7110** | `spc7110.cpp`, `spc7110dec.cpp` | Tengai Makyou Zero, Momotarou Dentetsu Happy, Super Power League 4 | Decompression chip (+ optional Epson RTC variant). Full register bank `$4800-$4842`, 64 KB decompression buffer, dedicated HiROM mapper. Slow on Old 3DS. |
| **OBC1** | `obc1.cpp` | Metal Combat | Small sprite-controller chip; RAM window at `$6000-$7FFF` (`MAP_OBC_RAM`). |
| **SETA ST-010/011/018** | `seta.cpp` (dispatch), `seta010/011/018.cpp` | F1 ROC II (ST-010), Hayazashi Nidan Morita Shougi (ST-011/018) | Register-block interfaces (`MAP_SETA_DSP` / `MAP_SETA_RISC`); ST-010 is the most complete implementation. |
| **S-RTC** | `srtc.cpp` | Dai Kaijyu Monogatari 2 | Real-time clock: 13 BCD nibbles, command state machine, advanced by wall-clock delta; **persisted inside the SRAM file** (extra bytes appended). |
| **BS-X / Satellaview** | `bsx.cpp` | BS Zelda, BS-X BIOS ("Sore wa Namae o Nusumareta Machi no Monogatari") | Modern 1.52-lineage implementation: dynamic MMC bank mapping, flash chip emulation, PPU-range registers. BIOS loaded from SD (`3ds/snes9x/BS-X.bin`, exact casing, or `BS-X.bios`). Many broadcast-era titles hang on time/broadcast-wait screens — see [Project Meta / Known Issues](project-meta.md). |

Not supported (no emulation and/or no input path): SNES Mouse, Super Scope, Justifier lightgun, Miracle Piano, exercise-bike carts — the corresponding titles are unplayable or need joypad patches.
