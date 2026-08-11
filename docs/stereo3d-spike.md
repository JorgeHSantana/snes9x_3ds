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
