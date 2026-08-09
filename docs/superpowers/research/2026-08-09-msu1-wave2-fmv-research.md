# MSU-1 Wave 2 (FMV/Data-Port) Feasibility Research Report

Date: 2026-08-09. Produced by a web-research pass to ground wave-2 planning: will the Old 3DS really struggle with MSU-1 FMV, and is there a lighter approach (analogous to the wave-1 NDSP audio offload)?

## 0. Grounding: what wave 1 already proved and what wave 2 changes

Wave 1 streams 44.1kHz/16-bit stereo PCM at ≈176 KB/s through buffered `fread` on the syscore mixing thread, fully decoupled from the emulation thread. The data port (`$2001`) is a single `fgetc()` per byte on the **emulation thread**, wired only for DMA transfer mode 0. Wave 2 needs transfer modes 1-4 and a byte-supply path fast enough not to stall Old 3DS.

## 1. How MSU-1 FMV actually works on real SNES hardware

**Port/DMA mechanics.** `$2001` is the auto-incrementing data-read register. DMA is hardware-locked to **8 master clock cycles per byte**, a hard ceiling of **≈2.68 MB/s** (byuu/Near, nesdev spec thread). CPU-polled reads: ≈0.51 MB/s.

**The real bottleneck is the PPU/V-blank budget, not the MSU-1 port.** NTSC V-blank (38 scanlines = 50,312 master cycles) allows **~5,000-6,000 bytes of VRAM DMA per frame** (up to ~11 KB with DMA+HDMA and overscan off) → a practical ceiling of **~300-660 KB/s** for anything writing video data into VRAM every frame.

**What real FMV hacks actually sustain:**
- **Bad Apple SNES demo**: needed ~7 KB/frame in theory, achieved ~6 KB/frame via a solid-tile dictionary/RLE scheme (90% of tiles solid) → **≈180 KB/s** — the same order as wave 1's audio rate.
- **Super Road Blaster**: early builds "didn't run in NTSC mode due to v-blank limitations" until preprocessing/compression fit the data inside V-blank. ~240×144 @30fps; full game data ≈453-852 MB, streamed from SD.
- Independent analysis (AtariAge): uncompressed 224×144×8bpp@30fps ≈ 945 KB/s video + 172 KB/s audio ≈ 1.12 MB/s — under the DMA cap but beyond V-blank; realistic target 4bpp @10-15fps, "comparable to Sega CD".

**Conclusion**: real FMV sustains **~150-400 KB/s** — only ~1.5-2× the already-solved audio rate, not an order of magnitude harder.

## 2. Existing emulator implementations

Nobody optimizes this path — all reference implementations read one byte per call:
- snes9x upstream `msu1.cpp`: `GETC_STREAM` per byte; **no MSU-1 DMA fast path** in upstream `dma.cpp` (only pointerable-memory sources get one).
- higan/bsnes (byuu's reference): `datafile.read()` per byte.
- Snes9x GX/RX (Wii): audio-only MSU work; no data-port optimization found.
- **SD2SNES/FXPak Pro (FPGA hardware)**: even it had a documented FMV corruption bug — [sd2snes issue #70](https://github.com/mrehkopf/sd2snes/issues/70) — traced to **SD access-time tail latency** (0.319 ms avg / 3.480 ms max on the failing card), not throughput. Lesson: **latency spikes, not steady-state bandwidth, are what break FMV**; read-ahead margin is the fix.

On PC/Wii the OS file cache makes per-byte reads irrelevant — the optimization has simply never been needed before. For a 268 MHz ARM11 it must be invented here.

## 3. Data-path cost analysis for our implementation

Current chain per DMA byte: `S9xMSU1ReadPort` → `msu1_read_port` → `fgetc` (stdio-buffered) — identical shape to upstream. At real FMV rates that is **150K-400K calls/s** of a 3-hop chain on 268 MHz — worth collapsing; the fix is cheap:

**Bulk read**: `msu1_read_data_bulk(state, uint8_t* dst, uint32_t count)` → **one `fread` per DMA transfer** (the source is always the same sequential run — only the B-bus destination pattern varies across transfer modes; the transfer count is known upfront). Collapses to roughly one fread per DMA instruction (a handful to dozens per frame).

**RAM precache feasibility**: Road Blaster is 453-852 MB — never fits (~10-20 MB spare). At 150-400 KB/s, 10-20 MB buys 25-130 s of video: viable for short-clip hacks only. Precache must be opportunistic (when `data_size` fits), falling back to streaming.

## 4. Lighter alternatives — where the audio analogy holds and where it breaks

Audio reads are decoupled (underrun = brief silence). **Data-port DMA is synchronous to the emulated CPU** — the bytes are needed *now*; a blocking SD read stalls the whole emulated system. So the read *call* can't be offloaded — but the **SD latency can be moved off-thread ahead of time**:

- **Read-ahead ring buffer filled by a background thread on the syscore** (the exact `APT_SetAppCpuTimeLimit` + `threadCreate` pattern wave 1 validated): the emulation thread's read becomes a `memcpy`. Seeks must invalidate + refill the window before DMA consumes (the sd2snes lesson; mirrors byuu's own buffer-before-playback guidance).
- **New 3DS core2** exists (exheader kernel flag `0x2000`) — extra headroom there; Old 3DS shares the syscore between audio mixing and prefetch, so both streams' worst case must be budgeted together.
- **3DS SD speed**: no published FS_Read benchmark exists; anecdotal Old-3DS-era performance ≈ class-4 (~4 MB/s sequential) — comfortably above 400 KB/s steady-state, but per-call IPC latency/tail spikes are unmeasured. **Biggest unverified risk: measure FS_Read latency distribution on real Old 3DS before sizing the ring buffer.**
- **Frame-skipping FMV** doesn't map to MSU-1 (the SNES side decides the DMA volume). The right lever is a **stall-not-corrupt watchdog**: on ring-buffer underrun, extend the emulated frame (brief hitch) rather than feeding stale bytes — correctness over pacing.

## Concluding assessment

**(a) Old 3DS feasibility**: cautiously positive — typical compressed FMV (~150-400 KB/s) is ~2× the proven audio rate. Aggressive/uncompressed corner cases (≥1 MB/s) are the real risk, mainly because 65816/PPU emulation already dominates the Old 3DS CPU. New 3DS should handle essentially everything.

**(b) Strategies, ranked:**
1. **Bulk DMA-span read** — one fread per transfer, sized from the transfer count. Cheap, safe, extends an existing upstream fast-path idea.
2. **Syscore read-ahead thread + ring buffer** — the true protection against the tail-latency failure mode that broke even FPGA hardware. Measure FS_Read latency first.
3. **Opportunistic whole-file precache** when the `.msu` fits spare RAM — free win for short clips, graceful fallback to (2).

**(c) Escape hatch**: per-game "Disable MSU-1" (already designed) as the hard fallback, plus a softer tier: the stall-not-corrupt watchdog turning SD hiccups into brief hitches instead of corruption.

## Sources

- https://forums.nesdev.org/viewtopic.php?t=11004 (MSU1 spec thread; DMA 8 cycles/byte, 2.68 MB/s cap)
- https://zumi.neocities.org/stuff/msu1_notes/ · https://github.com/Sunlitspace542/MSU-1-Docs · https://helmet.kafuka.org/msu1.htm
- https://forums.nesdev.org/viewtopic.php?t=10696 (Bad Apple: 6 KB/frame budget, tile dictionary)
- https://dforce3000.de/p_news_t_msu1.html · https://github.com/DocSchoko/SNES-SuperRoadBlaster (Road Blaster v-blank limits, sizes)
- https://nerdlypleasures.blogspot.com/2018/01/sd2snes-and-msu-1.html
- https://github.com/mrehkopf/sd2snes/issues/70 (SD tail-latency FMV corruption on FPGA hardware)
- https://www.zeldix.net/t2511-msu-1-video-on-fxpak-pro · https://www.zeldix.net/t1607-msu1-getting-started-guide · https://www.zeldix.net/t1809-rasberry-pi-and-msu-1 · https://www.zeldix.net/t1791-retroarch-cores-compatiblity-for-msu1-games
- https://raw.githubusercontent.com/snes9xgit/snes9x/master/msu1.cpp · https://raw.githubusercontent.com/snes9xgit/snes9x/master/dma.cpp · https://github.com/mdeguzis/higan/blob/master/msu1.cpp
- https://www.gc-forever.com/forums/viewtopic.php?t=3649 · https://github.com/niuus/Snes9xRX
- https://wiki.superfamicom.org/dma-and-hdma · https://sneslab.net/wiki/Direct_Memory_Access · https://snes.nesdev.org/wiki/Timing
- https://forums.atariage.com/topic/357133-tinkering-with-msu-1-fmv/
- https://www.3dbrew.org/wiki/Multi-threading (New3DS core2 exheader flag)
- https://gbatemp.net/threads/sd-card-speed-in-3ds.405244/ · https://gbatemp.net/threads/request-sd-card-reader-benchmark-tool.459377/ · https://gbatemp.net/threads/issues-with-certain-snes-games-on-retroarch-on-psvita.616670/
