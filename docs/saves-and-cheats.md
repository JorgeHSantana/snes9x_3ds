# Save States, SRAM, Screenshots and Cheats

[← Back to index](README.md)

## Save state format (`snapshot.cpp`)

Text-tagged chunked stream. Magic `#!snes9x:0001`; each block is a 3-char tag + `:` + 6 ASCII decimal digits (length) + `:` + payload.

Block order:

| Tag | Contents |
|---|---|
| `NAM` | ROM path |
| `CPU` | `SCPUState` core fields (flags, cycles, event machine) |
| `REG` | 65816 registers |
| `PPU` | full PPU register state |
| `DMA` | all 8 DMA channels |
| `VRA` | VRAM (64 KB) |
| `RAM` | WRAM (128 KB) |
| `SRA` | SRAM (128 KB) |
| `FIL` | `FillRAM` register mirror (32 KB) |
| `APU`/`ARE`/`ARA`/`SOU` | APU state, SPC registers, APU RAM (64 KB), sound DSP state — only if APU enabled |
| `SA1`/`SAR` | SA-1 state + registers — only for SA-1 games |
| `SP7`/`RTC` | SPC7110 state (+RTC) — only for SPC7110 games |

Field serialization is table-driven (`FreezeData` offset/size/type tables) and endian-normalized — the format is portable across hosts.

**Not serialized**: SuperFX GSU registers, C4, S-DD1, DSP-1..4, BS-X, OBC1, SETA state (S-RTC persists inside SRAM instead).

### Fork hardening

* The `CPU:` header shape is peeked and validated **before** `S9xReset()` is called, so a truncated/corrupt file doesn't destroy the running game.
* Extensive post-load fixups: `FixROMSpeed`, HDMA re-arm (`$420C`), DSP mirror re-sync (`IAPU.DSPCopy`), APU timer re-seed, `S9xFixSoundAfterSnapshotLoad`, vertical sections re-init, Mode 7 cache invalidation, brightness/color rebuild.
* Writing goes through `BufferedFileWriter`; the mixer is muted around the save ("fixes supposedly-silent paused scenes").
* The frontend detects a **broken-audio signature** (state saved with dead audio) before loading and asks for confirmation; details logged next to the state file. See [Platform Layer](platform-layer.md).

5 slots + an auto-save slot (written on exit when enabled); optional PNG screenshot per save, shown as previews in the menu.

## SRAM

* Auto-saved via `Settings.AutoSaveDelay` frames (setting: 1 s / 10 s / 60 s / disabled), plus on menu entry and HOME/sleep. Old 3DS users are advised to use larger intervals (SD writes pause emulation).
* During the write the audio mixer generates silence rather than stopping the NDSP channel.
* SRAM size/mask from the ROM header; per-game initial fill value (`SNESGameFixes.SRAMInitialValue`, default `0x60`).

## Screenshots

* Manual (hotkey/menu) → `screenshots/<rom>.<timestamp>.png`; save-state previews → `savestates/screenshots/<rom>/N.png` at 0.5 scale.
* Capture waits for the display transfer, undoes the frame-end buffer swap, reads back the framebuffer (BGR→RGB, un-swizzle) and encodes with libpng (compression level 1). Wide-mode captures average column pairs back to 400 px.

## Cheats (`cheats.cpp`, `cheats2.cpp`)

* **Formats**: Game Genie (`xxxx-xxxx`, substitution alphabet + address bit-shuffle) and Pro Action Replay (8 hex digits). A Gold Finger decoder exists but is not wired into the loaders.
* **Storage**: fork-added text format `.chx` (one CSV line per cheat: `Y|N,CODE,Name`) is preferred; the legacy binary `.cht` (28-byte records) is still read. Loader tries Game Genie first, then PAR; unparseable lines are skipped. Max 200 cheats (`MAX_CHEATS`, raised from stock 150).
* **Application**: `S9xApplyCheats()` runs every frame from `S9xEndScreenRefresh`. Direct-memory addresses are written through `Memory.Map[]`; register-space addresses route through `S9xSetByte`/`S9xGetByte` (timing-safe).
* **Search engine**: `S9xStartCheatSearch` + compare-by-value/change over WRAM/SRAM/APU-RAM snapshots, 8/16/24/32-bit, signed/unsigned (present in the core; the UI only exposes enable/disable).
* `S9xDeleteCheats()` runs on every ROM load — prevents stale cheats leaking across ROM switches (fork fix).
* Caveat from the README: cheats are lightly tested and can corrupt saves; the UI shows a cheats-active indicator in the menu.
