# Stereo 3D Spike — Notes (2026-08-11)

## Goal
Diorama-style stereoscopic 3D: SNES layers separated into Z-planes with
horizontal parallax, driven by the 3D slider.

## Prior attempt (Tyler Sanders, ../snes9x_3ds_3D, commit a540f27 "AI coded phase 1")
Ported here (shaders + uniform plumbing). Same matbo87 base, applied cleanly.
Only phase 1 of 3 was done; docs in that repo (plan.md/architecture.md).

## What this port ALREADY has (huge head start)
- `impl3dsSceneRender` (3dsimpl.cpp:764) reads `gpu3dsGetIOD()` and renders
  the scene TWICE (LEFT/RIGHT, `GPU3DS.activeSide`) with ±iod — but the
  parallax currently applies ONLY to the wallpaper (`img3dsDrawBackground`);
  the SNES screen quad is identical in both eyes (flat).
- LEFT/RIGHT render targets + gfxSet3D + dual clear all exist (3dsgpu.cpp).
- The tile renderer assigns per-priority depth to vertices (r0.z in shaders)
  — this is the Z-plane source for parallax.

## What the ported phase 1 contains (needs fixes)
- `stereoIOD` uniform declared in shader_tiles.v.pica (#6) and
  shader_mode7.v.pica (#7); ULOC_STEREO_IOD registered (3dsgpu.cpp/h).
- BUG 1: ULOC fetched only from the TILES program — mode7's location for its
  own uniform never fetched. Need a second enum entry (ULOC_STEREO_IOD_M7).
- BUG 2: their shader assembly flips the offset sign per SCREEN SIDE
  (x<0 vs x>=0) — that's a center-stretch, not stereo parallax. Correct:
  single `mad r0.x, r0.z, stereoIOD.x, r0.x` (per-EYE signed uniform),
  1 instruction, no branches. Rewrite before testing.

## Remaining design (the actual work)
1. Dual LAYER pass: `gpu3dsDrawSnesScreen()` (3dsimpl_gpu.cpp:457) draws the
   frame's vertex lists into SNES_MAIN (+SNES_SUB inside). Lists persist for
   the frame → callable twice. Plan: when iod>0, render layers with
   stereoIOD=-k into SNES_MAIN, composite LEFT; re-render with +k, composite
   RIGHT. Avoids a second 512KB VRAM texture (only ~730KB free) at the cost
   of pipeline ordering (target→texture→target flip twice per frame; the
   renderer already does this flip once per frame).
2. Set the uniform where SPROGRAM_TILES / SPROGRAM_MODE7 get bound during
   the layer pass; zero when slider off (parity: mad by 0 is identity).
3. 2D parity test: slider at 0 must be pixel-identical (Azahar screenshot
   diff vs feature/msu1 build).
4. Azahar can preview stereo: set render_3d/side-by-side in its config.
5. Old 3DS: slider forces iod=0 path → zero extra cost. 2DS: no slider.

## Open questions
- Depth values per layer/priority: read actual r0.z scale in gfxhw.cpp
  (depth constants per priority) to calibrate IOD_MAX_PIXELS.
- OBJ sprites all share priority-plane depth — acceptable diorama look?
- Color-math/brightness layers must NOT shift (they're screen-space) —
  likely need stereoIOD=0 for those draws (they render as LAYER_COLOR_MATH /
  LAYER_BRIGHTNESS in the same pass).

## SPIKE RESULT (2026-08-11): WORKING in Azahar
Dual layer pass implemented: left eye's pass runs with -iod before the left
composite (impl3dsRunOneFrame), right eye re-runs gpu3dsDrawSnesScreen with
+iod inside impl3dsSceneRender. Tiles shader = single `mad` (exact no-op at
slider 0). Uniform pushed via gpu3dsSetShaderAndUniforms on change/rebind.
Mode7 shader change from the prior attempt REVERTED (it was the texture
baker, wrong place — Mode 7 stays flat for now).

Measured in Azahar (side-by-side, factor_3d=80, MMX3 ZP intro): per-layer
dx between eyes = far city -2px, towers -12px, bridge -17px, dialog -19px —
monotone depth-ordered separation at a steady 60 FPS. Screenshot analysis
via gradient template matching (venv in scratchpad).

Next (hardware): judge the effect + comfort on a real New 3DS, decide the
parallax SIGN (current: front layers pop OUT of the screen; flipping the
two signs in 3dsimpl.cpp pushes the scene INTO the screen instead), maybe
scale per Intensity3D, and evaluate the shifted-edge artifact at the
viewport borders. Azahar preview knobs: qt-config.ini factor_3d=0..100,
render_3d=1 (side-by-side).

## Hardware validation - Old 3DS (2026-08-13)

Tested on a real Old 3DS: the full stereo pipeline (alternate-eye layer
pass, retained per-eye textures, dual composite) plus the per-scene
profile matcher run at full speed - "funciona muito bem" (Jorge).
The matcher itself is negligible by design (~15 byte reads + a few
64-bit compares per frame, log writes only on signature change), and
profile switches only touch the values the stereo pipeline already
consumes. This confirms the feature set is not New-3DS-only: the 804MHz
boost is not required for stereo 3D or scene profiles.
