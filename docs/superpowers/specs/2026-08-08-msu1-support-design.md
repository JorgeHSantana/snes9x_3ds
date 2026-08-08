# MSU-1 Support — Design

Date: 2026-08-08
Status: approved (brainstorming session with maintainer)
Target: personal fork (`feature/msu1` off `master`); upstream PR is a possible later step, out of scope here.

## 1. Goal

Add MSU-1 coprocessor support (CD-quality streamed audio + 4 GB data port) to snes9x_3ds, working on **both Old 3DS and New 3DS**, delivered in two waves:

* **Wave 1 — Music**: popular audio hacks (Zelda ALttP MSU, Chrono Trigger MSU, DKC…) play their PCM soundtracks correctly on both consoles. The data port is implemented **correctly** (full register spec) but not optimized.
* **Wave 2 — FMV/data throughput**: optimize the data path (DMA fast path, larger read-ahead) and benchmark FMV hacks (Road Blaster, Bad Apple) on Old 3DS. If Old 3DS cannot keep up, add a per-game boolean setting to disable MSU-1 as an escape hatch. Success: FMV fluid on New 3DS; best-effort + documented escape on Old 3DS.

Non-goals: `.msu1` ZIP pack support (no UNZIP in this fork), upstream submission, UI beyond the wave-2 escape toggle.

## 2. Requirements

* Works on Old 3DS (mixing thread on syscore with 45% CPU budget) and New 3DS.
* **Everything new has host-side unit tests; everything touched gets tests for the touched behavior** (project rule from this point forward).
* Follows `docs/CODING_STANDARD.md` (embedded style: no dynamic allocation at runtime, function-pointer backends instead of virtuals, references over pointers, all parameters validated, status returns).
* No regression when no `.msu` file is present (flag off ⇒ zero-cost path).

## 3. Chosen approach

**Dedicated NDSP channel 1 at 44.1 kHz, hardware mixing** (approach A of the brainstorm).

The MSU-1 PCM plays on NDSP channel 1 at its native rate; the 3DS DSP mixes it with channel 0 (SNES APU at 32 kHz) in hardware. The existing mixing thread (already on another core) feeds both channels. No CPU resampling (the 1.43 core has no resampler; Old 3DS cannot afford one).

Rejected alternatives: (B) mix into the 32 kHz stream like Snes9x RX/Wii — CPU resampling cost on Old 3DS syscore, invasive edits to `soundux.cpp`, quality loss; (C) preload PCM to RAM — YAGNI, tracks exceed available RAM anyway.

Reference implementations: snes9x upstream `msu1.cpp` (same license family) and Snes9x RX (Wii) — which proves synchronous buffered SD reads suffice on comparable hardware (~176 KB/s ≈ 3 KB/frame for audio).

## 4. Architecture

### New files

| File | Responsibility |
|---|---|
| `source/Snes9x/msu1.cpp` + `.h` | **The chip.** Register state machine (`$2000-$2007`), "S-MSU1" ID string, status bits, track selection, data seek, PCM header validation (magic "MSU1" + 32-bit loop point). Adapted from snes9x upstream to the 1.43 core. I/O via `FILE*` only; zero platform dependencies ⇒ compiles on host for tests. |
| `source/3dsmsu.cpp` + `.h` | **The audio bridge.** NDSP channel 1 management, PCM wavebuf queue, buffer fill from the mixing thread, and **state mirroring** (§6). All NDSP/libctru calls behind a function-pointer backend struct (`Msu1AudioBackend`) so the logic is host-testable with a fake backend. |
| `tests/` | doctest infrastructure: `tests/Makefile` (host g++/clang++), test suites, tiny `.msu`/`.pcm` fixtures generated at test runtime. |

### Touched files (minimal, guarded, delegating edits)

| File | Edit |
|---|---|
| `source/Snes9x/ppu.cpp` | `S9xGetPPU`/`S9xSetPPU`: dispatch `$2000-$2007` → MSU-1 when `Settings.MSU1` (currently open bus at `ppu.cpp:1095`). |
| `source/Snes9x/memmap.cpp` | Detection at ROM load: `<romdir>/<rombase>.msu` exists ⇒ `Settings.MSU1 = true` (flag already stubbed at `memmap.cpp:2783`). Reset flag on every load. |
| `source/Snes9x/dma.cpp` | B-bus `$01` reads from the MSU data port during DMA. Correct in wave 1, fast path in wave 2. |
| `source/Snes9x/snapshot.cpp` | New optional `MSU` block: current track, data seek offset, audio file position, status bits, volume, repeat/resume state. Absent block ⇒ MSU idle (back-compat both directions). |
| `source/Snes9x/snes9x.h` | `Settings.MSU1` flag (bool8, next to the other chip flags). |
| `source/3dssound.cpp` | Existing fences/start/stop also drive channel 1 via the `3dsmsu` API (calls only — no MSU logic here). |
| `source/3dsimpl.cpp` | Init/teardown, ROM-load/reset hooks → `msu3ds*` calls. |
| `source/3dsexit.cpp` | APT hooks (HOME/sleep/wake) → `msu3dsOnEvent`. |
| `Makefile` | Add the two new `.cpp` to `CPPFILES`; add `test` target delegating to `tests/Makefile`. |
| `.github/workflows/ci.yml` | Add a host-test job (ubuntu, native g++, `make test`) alongside the devkitARM build. |

### UI / settings

* Wave 1: **none**. Game controls its own volume via `$2006`; the global volume setting multiplies into the channel-1 mix.
* Wave 2 (contingent): per-game boolean "Disable MSU-1" escape hatch for Old 3DS, added to the Options menu + per-game config, only if benchmarks demand it.

## 5. Data flow

* **Emulation thread**: register writes mutate `Msu1State`. Track select opens `<romdir>/<rombase>-<N>.pcm`; data seek repositions `<rombase>.msu`. Filenames use the **exact ROM basename** — no region-tag trimming, no `mappings.txt` aliasing (hacks name their files to match the ROM file).
* **Mixing thread** (existing, other core): on each wake, besides channel 0, fills channel-1 wavebufs by reading from the `.pcm` stream through a dedicated 32 KB stdio buffer (≈ one physical SD read every ~10 frames), handling the loop point on EOF. No per-sample CPU processing.
* **Cross-thread contract**: play/stop/seek/track commands live in a small state struct guarded by the existing `snesAccessLock`, so MSU-1 **inherits the `snd3dsDrainMixing()` fencing automatically**. Single-word flags are `std::atomic`, matching `snd3DS.generateSilence`.
* **Data port reads** (emulation thread): buffered `fread` from the `.msu` stream via its own dedicated stdio buffer. Sequential reads after a seek are the common case (that is what the buffer optimizes).

## 6. ⚠️ STATE MIRRORING ON CHANNEL 1 — THE CRITICAL SECTION ⚠️

> **Channel 0 (SNES APU) carries years of accumulated state-handling fixes. Channel 1 inherits NONE of them for free.** Every event below MUST explicitly update channel 1, or the result is music leaking into the menu, desynced tracks after savestate load, or an orphaned channel playing over the next ROM.

| # | Event | Channel 1 must... |
|---|---|---|
| 1 | Menu entry / pause | Mute (mix 0), **keep position** |
| 2 | Menu exit / resume | Restore mix, resume |
| 3 | `snd3dsDrainMixing()` (any fence) | Stop filling wavebufs, output silence — same as channel 0 |
| 4 | Fast-forward (turbo) | Mute (channel 0 already mutes — mirror it) |
| 5 | HOME / sleep (APT hook) | Mute; restore on wake |
| 6 | ROM switch / reset | **Close files**, clear queue, zero state — nothing survives |
| 7 | Savestate load | Close current track, reopen saved track, **seek to saved position**, restore play/repeat/volume |
| 8 | Global volume change (settings) | Reapply mix = global volume × game volume (`$2006`) |
| 9 | Game writes `$2006` | Reapply mix (same formula) |
| 10 | App exit | Full teardown of channel and files |

**Enforcement (not dependent on human memory):**

1. **Single entry point**: `msu3dsOnEvent(Msu1Event)` — all 10 events are values of `enum class Msu1Event : uint8_t`. Existing call sites (`3dsexit.cpp`, `3dssound.cpp`, `3dsmain.cpp`, `3dsimpl.cpp`) call this one function. Nothing else touches channel 1.
2. **Mandatory matrix test**: a doctest suite iterates the **entire** `Msu1Event` enum against the fake backend and asserts the expected channel state (mix, queue, open files) after each event. A new enum value without a matrix row fails the test by construction.

## 7. Error handling

* Requested `.pcm` missing → "track missing" status bit; game continues (per spec; hacks handle it).
* Bad header (no "MSU1" magic / short file) → track rejected as missing.
* SD underrun (wavebuf starved) → silence for that interval, playback continues; underrun counter in the log for Old 3DS diagnosis.
* Data seek past EOF → clamped; busy bit never sticks.
* Savestate loaded with MSU files absent → audio stops, missing bits set, no crash.
* All parameter validation per `docs/CODING_STANDARD.md` §3 (null checks, range checks, status returns).

## 8. Coding conventions

`docs/CODING_STANDARD.md` governs all new code. Highlights as applied here:

* All buffers (PCM staging, stdio buffers, wavebuf memory) allocated **once** in `msu3dsInitialize()` (linearAlloc/static, like `snd3dsInitialize`), freed once in teardown. No runtime allocation.
* `Msu1AudioBackend` = struct of function pointers (`set_rate`, `set_mix`, `queue_buffer`, `clear_queue`, …). Production installs NDSP; tests install fakes. No virtuals.
* `FILE*` ownership: `Msu1State` is the single owner of both streams; every teardown path closes them; leak = failing test.
* Boundary naming: `S9xMSU1*` (core hooks), `msu3ds*` (platform entry points); standard naming behind the boundary.

## 9. Testing strategy

* **Host-side (doctest, `make test`, CI job)** — the bulk:
  * Register state machine: per-port read/write semantics, ID string, status bit transitions.
  * PCM header parsing/validation + loop-point behavior.
  * File discovery naming (`<base>.msu`, `<base>-<N>.pcm`, no trimming).
  * Wavebuf fill logic with fake backend + fake clock (including underrun path).
  * **The state-mirror matrix (§6)** — exhaustive over the enum.
  * DMA read path with in-memory fixtures.
  * Parameter-validation tests: every pointer/range-taking function fed garbage.
  * Init→teardown resource-ownership cycles.
* **Touched-code rule interpretation**: legacy files get 3–5-line delegating edits; the delegated-to function is fully tested with the same inputs the call site passes. Legacy code around the edit is not retrofitted.
* **Hardware validation (manual checklist per wave)**: ALttP MSU + Chrono Trigger MSU on Old 3DS and New 3DS — boot, track change, pause/menu, savestate round-trip, HOME/sleep, ROM switch, fast-forward.

## 10. Risks

| Risk | Mitigation |
|---|---|
| Old 3DS SD latency spikes starve channel 1 | Reads on the mixing thread (tolerates latency); wavebuf queue depth absorbs spikes; underrun counter to measure; wave-2 read-ahead tuning |
| State-mirroring gap discovered late | §6 single entry point + exhaustive matrix test |
| 1.43-core adaptation of upstream msu1.cpp diverges from spec | Port against the MSU-1 spec + cross-check with Snes9x RX behavior; register tests encode the spec |
| FMV throughput insufficient on Old 3DS | Wave-2 escape hatch setting (per-game disable) |
