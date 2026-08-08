# High-Level Architecture

[← Back to index](README.md)

Two halves communicate through a narrow bridge:

```
┌─────────────────────────────────────────────────────────────────┐
│  3DS platform layer (source/3ds*.cpp)                           │
│  3dsmain    — app shell, menus, emulation loop, frame pacing    │
│  3dsimpl    — THE BRIDGE: hooks between frontend and core       │
│  3dsgpu     — citro3d wrapper + packed render-state cache       │
│  3dsimpl_gpu, 3dsimpl_tilecache — layer batching, tile caching  │
│  3dssound   — NDSP + mixing thread (2nd core)                   │
│  3dsmenu/3dsui*/3dsthemes/3dsfont — second-screen UI            │
│  3dsinput/3dssettings/3dsconfig/3dsfiles/3dslcd/3dstimer/…      │
└───────────────▲─────────────────────────────▲───────────────────┘
                │ impl3ds* hooks + S9x* callbacks
┌───────────────┴─────────────────────────────┴───────────────────┐
│  Snes9x core (source/Snes9x/)                                   │
│  cpuexec/cpuexec-ops — 65c816 (ARM-register-pinned)             │
│  apu/spc700/soundux  — SPC700 + DSP sound                       │
│  ppu/gfx/gfxhw/tile  — PPU registers + HW renderer              │
│  memmap/dma          — memory map, ROM loading, DMA/HDMA        │
│  fx*/sa1*/dsp*/c4*/sdd1*/spc7110*/obc1/seta*/srtc/bsx — chips   │
│  snapshot/cheats     — savestates, cheat engine                 │
└─────────────────────────────────────────────────────────────────┘
```

The only core file that calls into the platform GPU API is **`gfxhw.cpp`** (~35 call sites into `gpu3ds*`/`cache3ds*`).

The bridge (`3dsimpl.cpp`) has two faces:

* **`impl3ds*` functions** consumed by `3dsmain` — initialize, run one frame, load ROM, save/load state, generate sound samples, take screenshots.
* **`S9x*` callbacks** required by the core — joypad reads, snapshot file I/O, messages, path helpers.

## Per-frame control flow

```
3dsmain: emulatorLoop()
  └─ impl3dsRunOneFrame()                          (3dsimpl.cpp:869)
       ├─ IPPU.RenderThisFrame = !skipDrawingFrame
       ├─ Memory.ApplySpeedHackPatches()           (first frame only)
       ├─ gpu3dsPrepareSnesScreenForNextFrame()    (flips VBO halves — MUST precede main loop)
       ├─ S9xMainLoop() | S9xMainLoopWithSA1()     (cpuexec.cpp)
       │    ├─ per opcode: cycles >= NextEvent → S9xDoHBlankProcessing()
       │    │    ├─ HBLANK_START → S9xDoHDMA()
       │    │    ├─ HBLANK_END   → S9xSuperFXExec(), S9xUpdateAPUTimer(),
       │    │    │                 deferred $212C-$2131 flush, V_Counter++,
       │    │    │                 S9xStartHDMA(), RenderLine(), NMI arm, …
       │    │    └─ HTIMER_*     → H-beam IRQ
       │    └─ PPU register write → FLUSH_REDRAW → S9xUpdateScreenHardware() (gfxhw.cpp)
       ├─ gpu3dsFrameBegin() → gpu3dsDrawSnesScreen() → impl3dsSceneRender() → gpu3dsFrameEnd()
       └─ (screenshot capture if requested)

audio thread (core 1/2): NDSP frame callback → LightEvent → snd3dsMixSamples()
  └─ impl3dsGenerateSoundSamples() → S9xSetAPUDSPReplay() + S9xMixSamplesIntoTempBuffer(512)
  └─ impl3dsOutputSoundSamples()   → master volume/echo/FIR → interleave → ndspChnWaveBufAdd()
```

Key observations:

* **Rendering is driven by the core, mid-frame**: `FLUSH_REDRAW()` fires whenever a PPU register write would change how the screen section rendered so far looks, so one SNES frame may be rendered in several horizontal slices with different register states. This is how mid-frame effects (HDMA gradients, split screens) work on the GPU path.
* **Frame skipping skips only drawing** (`IPPU.RenderThisFrame = false`); emulation always runs every frame.
* **Audio is pull-based from another core**: the emulation thread never blocks on audio; the mixing thread pulls samples on NDSP's ~5 ms cadence. Cross-thread safety comes from the deferred DSP write queue and the `snd3dsDrainMixing()` fence (see [Audio and Timing](audio-and-timing.md)).

## Application state machine

`GPU3DS.emulatorState`: `EMUSTATE_PAUSEMENU` ⇄ `EMUSTATE_EMULATE` → `EMUSTATE_END`. The main loop in `main()` dispatches `showMenu()` / `emulatorLoop()` accordingly. APT hooks (HOME, sleep, power) push the state back to the pause menu and fence audio/LCD state (see [Platform Layer](platform-layer.md)).
