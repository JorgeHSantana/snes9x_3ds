# Issues #78 / #79: first implementation batch

Baseline: `bbef7fae6ddfc99eb4fc848f27afa7cc70983c8d`.

## Implemented changes

| Area | Change | Remaining limitation |
|---|---|---|
| SRAM (#78) | Complete-write and close checks; sticky buffered errors; dirty retained on failure; silence restored | Still synchronous, no atomic file replacement; hardware I/O latency not measured |
| Savestates/rewind (#78) | Propagate file close errors; reuse bounded core-owned serialization workspace, preserving zero padding | `fmemopen` still allocates on memory-load; no claim of removing all rewind allocations or latency |
| Mode 7 Direct (#78) | Select minimum squared span, then one reference square root per run | No claimed FPS percentage |
| Mode 7 Layer (#78) | Distance effects require Direct; stale hidden M7FX cannot select that branch | Ordinary layer effects remain available |
| Build (#78) | Release requires custom citro3d dependency | Hardware performance still needs measurement |
| Sprite evaluation (#79) | Appearance-only OAM changes reuse geometry lists | Position, size, VFlip, interlace and priority-rotation invalidation retained |

## Sprite dependency audit

`S9xSetupOBJ` consumes HPos, VPos, Size and VFlip plus global size,
interlace and rotation state. It computes widths/heights, visible tile counts,
scanline sprite indices/rows, range/time-over flags and the wrap fallback.
It does not cache tile number, palette, per-sprite priority or HFlip.
`S9xDrawOBJSHardware` reads those appearance fields when emitting geometry.

The low-OAM handler still flushes **before** changing any OAM field. It only
avoids setting OBJChanged for appearance-only changes; it never clears an
already-pending invalidation. High OAM and rotation branches are untouched.
This does not group sprites, move their stereo depth or reorder OBJ.

## Host validation

Final host run: 208 cases / 139414 assertions, including ASan+UBSan.
Startup config reads are explicitly tested with an absent write buffer;
writable opens must fail before truncation in that condition.
The ARM ELF reserves 65,596 bytes of BSS for the serialization workspace
(64 KiB + 60 bytes); this is a deliberate resident-memory tradeoff, not
a reduction in steady-state RAM use.

Run `make -C tests test`. New cases exercise real stdio write failure, fake
open/short-write/close failures, retry/silence state, memory-writer overflow,
all 65,536 attribute-byte transitions, all 128 sprite offsets, and equivalent
Mode 7 encoded depth across 128 runs of 512 spans. These are unit tests, not
an exhaustive ROM compatibility test or hardware benchmark.

## Visual validation procedure

Build baseline and candidate with the same flags:
`-DPROBE_FBDUMP -DPROBE_FORCE_SLIDER -DPROBE_SBS` (clean on flag changes).
Use `tools/azahar-validate/frame_probe.py SCENE --dsx PATH --out DIRECTORY`.
The runner uses frame 600, restores prepared SD files and stops only its own
Azahar process. A native startup warning may require acknowledgement.

Compare baseline/candidate for `smk-m7-direct`, `smk-m7-layer`, and
`mmx3-pillar-blur6`; compare candidate `smk-layer-fx0` / `smk-layer-fx1`
to ensure the hidden option no longer affects Layer. Record actual results
in the journal, not merely successful boot. Builds used for publication must
be clean and omit all PROBE flags.

Completed with candidate `b893227` and ARM CI run `34665296669`: exact RGB
equality in all three baseline/candidate comparisons; exact full savestate
equality and successful save/load in each scene. Layer M7FX=0/1 is also RGB
identical. See the journal and committed captures under
`tools/azahar-validate/goldens/optimization-78-79/` for the evidence.

## Work explicitly still open

The issues are broad investigation tracks, not all proven optimizations.
This batch does **not** implement asynchronous SRAM, log
buffering, sparse Mode 7 reverse indices/bakes, DMA batching, mixer/FIR
rewrites, CPU/APU dispatch experiments, tile-conversion alternatives or
optional UI/FLAC allocation changes. None is claimed solved by passing the
existing tests. Further candidates need bounded prototypes and their own
correctness/performance evidence. Old/New hardware timings, audio underruns,
frame-time tails and compatibility scenes remain required.

## Issue #63

The original f4mrfaux `ef11ac0` SuperFX series is already cherry-picked as
`738dbc5`. The three fx source blobs match exactly. Switch dispatch and
program-bank pointer fetch are present; reapplying them is not new work.
The open follow-up concerns further Star Fox optimization. Keep that separate
from verification of the already-delivered series and do not invent a speedup.
