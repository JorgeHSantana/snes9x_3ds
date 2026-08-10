# MSU-1 Wave 2 Phase A — FMV Data-Port Design

Date: 2026-08-10. Approved scope: incremental Phase A only (bulk reads + DMA transfer modes). Reference research: `docs/superpowers/research/2026-08-09-msu1-wave2-fmv-research.md`.

## Goal

Make MSU-1 FMV hacks (reference: Super Road Blaster) render video by (1) honoring DMA transfer modes 1-4 for the MSU-1 data port and (2) collapsing per-byte `fgetc` reads into bulk `fread` chunks.

## Constraints

- New 3DS is the performance target; Old 3DS is best-effort in Phase A.
- Coding standard applies (`docs/CODING_STANDARD.md`): RTTI/exceptions stay off, host tests for everything new.
- No threads, no ring buffer, no RAM precache, no stall watchdog in Phase A (deferred to Phase B, only if hardware validation shows the need).
- Wave-1 behavior must not regress: audio streaming, savestates, mode-0 DMA, `$2001` CPU-polled reads.

## Current state (wave 1)

- `dma.cpp` MSU-1 branch (`S9xDoDMA`, ~line 121): detects a fixed A-bus source in `$2000-$2007`, then loops `count` bytes of `S9xMSU1ReadPort` → `S9xSetPPU(0x2100 + d->BAddress)`. The B address never varies, so only transfer mode 0 works. FMV needs mode 1 (`$2118/$2119` alternating).
- `msu1.cpp` data port (`$2001`): one buffered `fgetc` per byte.

## Design

### 1. Bulk data read (msu1 core — host-testable)

```c
// Reads up to count bytes from the data track at data_pos via one fread.
// Returns bytes actually read; advances data_pos by that amount.
// Short reads happen at EOF; caller decides padding. No status flags change.
uint32_t msu1_read_data_bulk(Msu1State& state, uint8_t* dst, uint32_t count);
```

Semantics identical to `count` successive port-1 reads (same `data_pos` advance, same EOF behavior — port 1 returns 0x00 past EOF, so the DMA caller pads short reads with 0x00). Data seeks via ports `$2000-$2003` are unaffected: they reposition `data_pos`/`data_file`, and the next bulk read continues from there.

### 2. DMA transfer-mode patterns (dma.cpp + pure helper)

```c
// B-bus register offset of the i-th byte for a given DMA transfer mode.
// Mode 0:B / 1:B,B+1 / 2:B,B / 3:B,B,B+1,B+1 / 4:B..B+3; 5-7 mirror 1,2,3.
uint8_t msu1_dma_b_offset(uint8_t transfer_mode, uint32_t byte_index);
```

Lives in the msu1 core so it is host-testable; `dma.cpp` calls it.

The MSU-1 DMA branch becomes:

- If the fixed A-source is exactly the data port `$2001`: consume the transfer in chunks of 512 bytes on a stack buffer filled by `msu1_read_data_bulk` (pad short reads with 0x00), writing each byte with `S9xSetPPU(value, 0x2100 + d->BAddress + msu1_dma_b_offset(d->TransferMode, i))`.
- Any other port (`$2000`, `$2002`, ...): keep today's per-byte `S9xMSU1ReadPort` loop, now also applying `msu1_dma_b_offset` for the B side.
- Cycle accounting unchanged: `CPU.Cycles += (count + 1) * SLOW_ONE_CYCLE` and `S9xUpdateAPUTimer()` as today.

### 3. Testing

Host doctest additions (`tests/`):

- `msu1_read_data_bulk` equals N successive port-1 reads on the same file (content + `data_pos`).
- Short read at EOF returns the truncated count; subsequent reads return 0.
- Seek (ports `$2000-$2003` write sequence) followed by bulk read starts at the new position.
- `msu1_dma_b_offset` full table for modes 0-7 over at least 8 byte indexes.

### 4. Validation

1. Azahar (3DS emulator on the Mac): run Super Road Blaster with the built `.3dsx`/`.cia`; video must play, audio in sync. This is the fast iteration loop.
2. User validates on real New 3DS hardware.
3. Regression: MMX3 MSU-1 music still works (Azahar + hardware); host suite green; Docker build clean.

## Out of scope (Phase B triggers)

Syscore read-ahead thread + ring buffer, opportunistic whole-file precache, stall-not-corrupt watchdog — only if Phase A stutters on hardware. FS_Read latency measurement precedes Phase B sizing.
