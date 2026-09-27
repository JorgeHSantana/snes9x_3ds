# Field diagnostics: logs, numbers, probes, sending

How a problem on a console reaches the developer, and what the emulator
measures about itself. State as of Stable v2.4 (2026-09-27).

## 1. The session log

`Enable Logging` (Emulation tab) writes `sd:/3ds/snes9x_3ds/debug_v<ver>_session.log`,
truncated at every start; the choice is latched at boot (restart after
changing it). Lines carry `[SS.mmm]` since start, or `       | ` when
several land in the same millisecond.

The log never touches the SD from the emulation thread (issue #59): lines
go to a two-chunk spool (`log_spool.h`, 2 × 32 KiB) and a lowest-priority
writer thread on the mixer's core drains a chunk when it fills or once a
second (`3dslog.cpp`, started by `snd3dsInitialize`; on the caller's core
when NDSP is absent, e.g. Azahar). A writer that falls behind costs
dropped lines, counted and announced by the next line that fits
(`[log] N lines dropped`), never a wait. The crash tail is kept the old
way: menu iterations, exit, ROM unload, the updater and autosave call
`log3dsFlush()`, which waits for the writer and drains synchronously.
The `[sig]` register-signature line is throttled (8 per 60 frames, a value
flipping every frame logged once, a summary of what was dropped).

## 2. Numbers in the log

| line | cadence | meaning |
|---|---|---|
| `[perf][frame] 300 frames: load avg=N% max=N% over-budget=N skipped-draws=N blur=…` | 5 s | the emulation thread's share of the frame budget (input + core + render submission, not the vsync wait); frames over 100 %; draws the pacer skipped; Blur Auto tier. Old 3DS, Mario Kart race: 95–99 % |
| `[perf][rewind] n=8 busy= failed= freeze= delta(worker)= key= thumb= emu=` | 8 captures | capture cost; `emu` is what the emulation thread paid, the encode runs on the worker |
| `[rewind] capture slow: Nms (…; pre, freeze, begin, thumb, encode …)` | when ≥ 8 ms | where a slow capture's time went |
| `[perf][sram] …` | per autosave | SRAM write cost (async: `queued copy`) |
| `[blur] auto -> light/full/off` | on change | Blur Auto tier decisions |
| `[msu1] …`, `MSU-1 underruns: N` | events / unload | MSU-1 audio health |

## 3. Profile build

`make 3dsx cia EXTRA_DEFINES=-DPROFILE_LOG` turns every `t3dsTimer` on and
writes the 120-frame breakdown to the log (`[profile] …`: S9xMainLoop,
S9xUpdateScreen, draws, GPU wait, flush, runOneFrame). A field build for
one measurement, never a release: each bucket costs a syscall per call.
Deliverables go to `output/snes9x_3ds_profile.{3dsx,cia}`.

## 4. Sending from the console (issue #80)

With `sd:/3ds/snes9x_3ds/github.env` present and complete, the Emulation
tab shows **Send Log to GitHub** and **Send Crash Dump to GitHub** under
Enable Logging. The file (`github.env.example` in the repo root):

```
GITHUB_TOKEN=github_pat_...
GITHUB_COMMENTS_URL=https://api.github.com/repos/<owner>/<repo>/issues/<n>/comments
```

Use a fine-grained personal access token limited to Issues (read/write)
on that one repository: the console does not verify the server
certificate (issue #82), so the token's reach is the safety margin.

Each send is one issue comment: a header (emulator version and sha,
console model, clock mode New 804 MHz + L2 / Old 268 MHz, date), then the
payload in a code block. The log is the current session's, flushed first,
its last 60 KB cut at a line start (GitHub caps a comment at 65536
characters). The crash dump is the newest `*.dmp` in `sd:/luma/dumps/arm11`
by modification time, base64-encoded. The post runs on a worker thread
behind a dialog; the result shows the comment URL or the HTTP status.
Code: `github_env.h`, `github_report.h` (pure, tested), `3dsgithub.cpp`,
`update3dsNetPostJson` in `3dsupdatenet`.

Without the file, or before the token exists, the homebrew ftpd exposes
the SD over Wi-Fi: `curl ftp://<console-ip>:5000/3ds/snes9x_3ds/debug_v2.4_session.log`.

## 5. Azahar probes (developer machine)

`tools/azahar-validate/validate.py run <scene> [--fbdump] [--dsx <build>]`
boots a scene (ROM + optional savestate) in Azahar and reads the session
log. Probe builds (`EXTRA_DEFINES`, all `#ifdef`, never in a release):

| define | effect |
|---|---|
| `PROBE_FORCE_SLIDER` | 3D slider forced on |
| `PROBE_FBDUMP` | the emulator saves its own top screen at frame 600 (and does a savestate round trip) |
| `PROBE_FBDUMP_EVERY=N` | dumps frames 120..720 every N (needs `PROBE_FBDUMP`) |
| `PROBE_HOLD_DOWN` | holds D-pad Down for frames 120–599 (needs `PROBE_FBDUMP`) |
| `PROBE_INIDISP_LOG` | $2100 writes, black sections, segments, window registers, CGRAM per frame |
| `PROBE_NO_MSU_BLANK_HACK` | MSU-1 forced-blank suppression off |
| `PROBE_DMA_PERF` | DMA cost per class per 60 frames, section renders inside DMA |
| `PROBE_SECTION_LOG` | what triggers each section render and its phases; Mode 7 dirty chars |
| `PROBE_GITHUB_SEND` | posts the log at frame 300 and the dump at 360 (point `github.env` at a local HTTP server) |

Azahar's `svcGetSystemTick` derives from emulated cycles: `[perf]` numbers
are deterministic per scene, an exact A/B without noise (one svc costs
~0.7 µs emulated, so per-byte timers inflate what they measure). The
Makefile does not track `EXTRA_DEFINES`: remove `build/{3dsimpl,dma,gfxhw,ppu,gfx}.o`
when the define set changes, or the probe silently stays out.

Frame dumps are compared pixel for pixel against a baseline build (the
last release) on a fixed scene set: Mario Kart race and boot, MMX3 pillar,
Zelda MSU transition. A savestate scene does not cover boot-time state
transitions (the Nintendo-logo regression of 2026-09-26 only showed on
`smk-boot`).
