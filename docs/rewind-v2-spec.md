# Rewind v2 — Final Specification

Agreed 2026-08-16. Supersedes the hold-to-rewind gesture and the earlier
timeline drafts. Issues: #36 (engine), #42 (UI), #37 (delta, absorbed here
as the window-tick format).

## A. Recording during play

1. **Input tape** — always on: effective pad state at the core boundary
   (post-turbo), 4 bytes/frame, **plus every MSU-1 port read with the value
   returned** (replay feeds them back; closes the SD-stall divergence).
   ~0.5 KB/s.
2. **Anchors** — full savestates (~450KB). Full on purpose: an anchor is the
   root every reconstruction starts from and must not depend on another
   snapshot (no delta chains); at 20s spacing delta would compress poorly
   (~50%) and anchors are few (8–16 = 3.6–7MB), so the robustness is cheap.
   Captured opportunistically (vsync-headroom frames; forced at the ceiling).
   **Spacing is adaptive**: target = tolerated first-jump latency × measured
   replay speed, clamped (≈5s–30s); starts conservative (~10s).
3. **Replay-speed meter** — every re-simulation is timed (frames replayed ÷
   wall ticks) into a moving average; drives anchor spacing and Loading
   estimates. **Old 3DS**: if hardware confirms <1× replay, Old stays on the
   snapshot-ring engine — same UI, different moment provider.
4. **Thumbnails** — decoupled from anchors: one per timeline tick
   (calibrable, start 1s), CPU decimation of the finished framebuffer
   (100×60 RGB565, 12KB, ~0.1ms), memory-capped (≈4MB ≈ 5.5min). Ticks
   beyond the cap materialize their thumb on first visit (self-healing). If
   they ever measure expensive, they are dropped.

## B. Materialization window (the "n-1, n, n+1" model)

- A **segment** is the stretch between two anchors *plus the set of
  interpolated tick-states filled from it* — n names the segment, never a
  single frame.
- The live window is **n-1, n, n+1** around the cursor's segment. Anything
  outside is discarded (memory freed).
- **Sliding**: cursor crosses into n-1 → it becomes the new n, the old n+1
  is deleted immediately, and the new n-1 starts filling automatically in
  the background — the goal is that a user navigating at human pace **never
  sees "Loading"**. Symmetric when crossing into n+1.
- **Filling**: enter a segment → load its anchor → replay forward, dropping
  a tick-state at each tick. Neighbours fill in bounded slices during UI
  idle time; never mid-press. "Loading..." (pause-style bar on the dark game
  screen) appears only when navigation outruns the prefetch.
- **Budget**: window tick-states stored as **delta against the segment's
  anchor** (~70KB vs 512KB; adjacent ticks compress well) → 3 segments ×
  20s × 1s ticks ≈ 4.2MB + 3 anchors. Fallback if delta lands later:
  full-state ticks with shorter segments (≈8s) — same UI, one constant.
- Leftward scrubbing is cheap *by construction*: the segment is already
  materialized when the cursor moves inside it.

## C. UI — the emulator's own interface, nothing reinvented

**Entries**
- **"Rewind"** action in the Emulator section, next to Save/Load State (not
  inside them).
- The **press hotkey** opens the same screen. **The hold gesture is dead.**
- **F0**: on entry a state of the present is saved. Cancelling = reload it.

**On open**: game freezes; the action screen goes dark with **"Select a
moment below"** — same font, same centered bar, same stereo-3D effect as
"Press START to resume". Audio silent; MSU latched. Window filling starts.

**Bottom screen**: the timeline, ticks of 500ms or 1s (calibrate), thumbs
on ticks, icons and bottom-bar hints identical to the main menu (same font,
same placement).

**Navigation**: left = older, right = newer, auto-repeat on hold.

**B while navigating** (context-sensitive): entered via hotkey → straight
back to the game (F0 reload); entered via menu → back to the menu.

**A while navigating**: materializes the frame on the game screen (real
state, one frame rendered). The bottom screen shows the emulator-styled
**Yes/No** dialog (theme colors, accent bar, Ⓐ/Ⓑ bottom bar), default
**No**. **No** (or B) → back to the timeline where it was.

**Yes**: the bottom screen closes back to the emulator wallpaper. On the
game screen, **"Resuming in 3... 2... 1..."** — pause-style bar, step
configurable (Off/250ms/500ms/1s). **Point of no return: no B, no input
read at all during the countdown.** At Go!: tape truncated at the chosen
point and later anchors/tick-states/thumbs discarded together (the moment
becomes the present and remains rewindable); MSU applies its latched state
once; audio resumes; the confirming press is swallowed; play continues from
the exact frame shown.

**Any savestate load (slot, quick, auto) resets the whole algorithm** —
tape, anchors, window, thumbs. History restarts from that point. ROM switch
and console reset already did.

## D. Implementation ladder (each step shippable)

1. **Final UI over the current snapshot engine** (tick = capture cadence,
   no replay): validates the full UX at zero risk.
2. **Tape + MSU port reads + replay-speed meter** — invisible recording, no
   behavior change.
3. **Anchor + window engine on New 3DS** (Old stays on snapshots); same UI,
   different moment provider.
4. **Delta tick-states** (window budget) + final constant calibration
   (tick, segment, thumbs).
