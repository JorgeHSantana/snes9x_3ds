# Audio and Timing

[← Back to index](README.md)

## Audio pipeline (`3dssound.cpp` + core `soundux.cpp`)

### Output

* **NDSP** (since v1.61; CSND before), channel 0, 32 kHz stereo PCM16, `NDSP_INTERP_POLYPHASE` interpolation.
* One `linearAlloc` carved into up to 16 wave buffers of 256 frames each. The "Audio Buffer Size" setting selects 4/8/16 buffers ≈ 32/64/128 ms latency.
* `ndspInit()` failure is non-fatal — the app runs silent (DSP firmware `3ds/dspfirm.cdc` is required for sound on console).

### Threading

* Mixing thread on **core 2** (New 3DS) or **core 1/syscore** (Old 3DS, enabled by `APT_SetAppCpuTimeLimit(45)`; the limit is restored when the HOME menu takes over). Thread priority `0x18`.
* The NDSP frame callback (DSP service thread, ~5 ms cadence) only signals a `LightEvent`; the mixing thread wakes on it and refills every DONE/FREE wave buffer:
  1. `impl3dsGenerateSoundSamples()` → `S9xSetAPUDSPReplay()` (drains the deferred DSP register write queue — see [Emulation Core](emulation-core.md)) + `S9xMixSamplesIntoTempBuffer(512)`.
  2. `impl3dsOutputSoundSamples()` → `S9xApplyMasterVolumeOnTempBufferIntoLeftRightBuffers()` (master volume, echo feedback, 8-tap FIR filter, clipping) → interleave L/R into the wave buffer.
  3. Cache flush (`svcFlushProcessDataCache` on hardware, `DSP_FlushDataCache` on emulators) → `ndspChnWaveBufAdd()`.

### Synchronization

* The emulation thread **never blocks on audio** — the mixer pulls samples at its own cadence and reads live APU state under `snesAccessLock`.
* All teardown paths (ROM switch, reset, savestate save/load, screenshots, menu entry) are fenced with `snd3dsDrainMixing()` — sets a `generateSilence` flag, then takes/releases the lock as a barrier — and `snd3dsResumeMixing()` afterwards. This replaced an older sleep-based approach that could race and crash on ROM switch.
* Silence is generated when paused, during SRAM autosave, and during fast-forward (turbo mutes).
* Volume gain (up to 2.0×) is applied **post-resample** via `ndspChnSetMix`, not pre-resample, to avoid clipping-induced aliasing.

## Timing and frame pacing

Two cooperating mechanisms:

### 1. LCD refresh retuning (`3dslcd.cpp`)

The 3DS LCD defaults to **59.831 Hz**, which doesn't match the SNES. `lcd3dsSetEmulationRate()` rewrites the PDC `VTotal` hardware registers (`0x400424` top / `0x400524` bottom) so the panel physically refreshes at the SNES rate:

```
TICKS_PER_SEC             = 268123480    (ARM11 tick rate)
TICKS_PER_FRAME_SNES_NTSC = 4462088      → 60.098814 Hz  (21477272 / (1364 × 262))
TICKS_PER_FRAME_SNES_PAL  = 5361734      → 50.006978 Hz  (21281370 / (1364 × 312))
```

`vtotal = round((VTotal+1) × 59.831 / targetFps) − 1` (~0.02 Hz residual error). The top screen's value is `vtotal×2+1` in 3D/wide mode. VBlank-waiting then paces at the *exact* SNES rate — no periodic dropped or duplicated frames.

Skipped entirely on emulators (`!GPU3DS.isReal3DS`). Restoration on exit/HOME recomputes the value from the **current** top-screen mode — restoring a stale 3D-mode value while in 2D leaves the panel in a broken timing state.

### 2. `paceFrame()` (`3dsmain.cpp:2086`)

Accumulator of actual vs ideal ticks per frame (`skew = ideal − actual`):

* **Running slow** (`skew < −TicksPerFrame/10`): skip the next frame's *rendering only* (emulation always runs), up to `MaxFrameSkips` (0-4, per-game); when the budget is exhausted, accept slowdown and reset the window.
* **Running fast**: sleep the skew in nanoseconds (`FrameSync::Sleep`, also used on emulators) or wait for VBlank (`FrameSync::VBlank` — meaningful because of the LCD retuning above).
* **Turbo mode**: renders every other frame, uncapped.

`settings3DS.TicksPerFrame` is derived in `settings3dsUpdate()` from the ROM region and the per-game "Framerate" setting (which can force 60 FPS for PAL games).

## Profiling (`3dstimer`)

10 timer buckets (main loop, SuperFX, screen update, draw phases, GPU wait, flush, whole frame) over a 120-frame window. Compiled to nothing unless `PROFILING_DISABLED` is undefined in `3dstimer.h`. Toggled in-game with SELECT+L+Right / SELECT+L+Left; output goes to a console on the second screen or the log file. The whole-frame bucket is reported as "max potential fps" since it stops before `paceFrame()`.
