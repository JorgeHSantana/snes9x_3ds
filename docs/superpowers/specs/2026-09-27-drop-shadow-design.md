# Drop shadow per depth row (issue #77, v4: depth EQUAL)

A marked layer/priority row casts a silhouette onto the layer behind it
in the profile's depth gauges (Mega Man X3: X's outline on the wall).
Glow and per-sprite marks stay out (the per-sprite signature machinery
of #76 is gone).

## Model
- 12 rows: BG1..BG4 x priority 0/1, Sprites priority 0..3. A profile
  holds a 12-bit `ShadowRows` mask plus `ShadowX`, `ShadowY` (-4..4 px,
  default 2, 2; one light per scene), `ShadowColor`, `ShadowSmallOnly`.
- The receiver of a row is the nearest row BEHIND it in the gauges
  (largest depth smaller than its own, among rows the scene uses) plus
  every used row sharing that depth: one surface in 3D. The gauges, not
  the SNES order, say what is behind (Jorge): a sprite the SNES draws
  under BG1 still casts on BG1 when BG1's gauge is behind it. Only a row
  sharing the caster's own depth plane is skipped (indistinguishable
  from its body).
- The depth buffer already holds, per pixel, the plane of the visible
  surface by the end of the BG passes: sprites write `(prio+1)*3`, BG
  tiles their mode's d0/d1 (`gfxhw.cpp` depth tables, now mirrored in
  `GPU3DS.stereoBgPlane`) under GEQUAL, transparent texels and windowed
  pixels never write. The colour-math and brightness rects then write
  z = 0 over the whole screen - which is why the first measurement
  ("GEQUAL and LEQUAL pass everywhere") saw nothing: it ran after them.
- One phase per target, after the last BG and before colour math. Per
  casting tier, per receiver plane: the caster's own geometry (its
  sections, or OBSEL-small sprite spans) drawn once more with the depth
  map at scale 0 / offset plane/32 (`C3D_DepthMap`, so the shader is
  untouched and every vertex lands at exactly the receiver's plane),
  depth func EQUAL without depth write, alpha test NE_ZERO, TexEnv
  stage 3 = colour at texture alpha x 0.5, normal colour blending, the
  section's own window stencil, at the receiver's parallax plus
  Shadow X/Y (Y through the spare `w` of `stereoIOD`).
- Per pixel: a layer in front holds a larger plane (fails), the caster's
  body its own plane (fails: nothing redraws the caster, its stereo
  position is untouched), a hole in the receiver a smaller plane or 0
  (fails). The Mode 7 plane receives like any BG (its lines write their
  plane); it never casts.
- Cost per casting tier and eye: the caster's vertices once per receiver
  plane (typically 1-3). No rects, no BG passes, no erase, no stencil.
- Only in 3D (slider on). No Blur Auto gate (its tier flips switched
  the shadow on and off).

## Dropped on the way
- v1: silhouette after the BGs + caster redraw (big sprites lost their
  shadow, the caster moved in stereo).
- v2: stencil bits 0-1 (Azahar changed between equivalent builds, the
  console flickered).
- v3: alpha mask through the game texture after colour math (a rect, a
  full pass of every BG of the plane, an erase and N tiers per casting
  layer: too heavy for an Old 3DS at 95-99%, and wrong/flickering on
  the console).
- A new vertex uniform: `add r0.y, uniform.w, r0.y` is mis-encoded by
  picasso (a 4 px shift); the y offset goes through a register copy.

## Files
- `stereo_shadow.h`: row index, mask helpers, behind-row lookup, row ->
  plane, receiver planes (pure, tested in `tests/test_stereo_shadow.cpp`).
- `3dsgpu.h/.cpp`: `GPU3DS.stereoBgPlane[8]`; `gpu3dsShadowPassState(plane)`
  / `gpu3dsShadowPassEnd()` (depth map, EQUAL, blend, applied-state poison).
- `Snes9x/gfxhw.cpp`: the BG and Mode 7 draw macros fill `stereoBgPlane`.
- `3dsimpl_gpu.cpp`: `drawShadowPhase`, called at the first colour math /
  brightness layer of each target (fallback after the loop);
  `-DPROBE_SHADOW_LOG` logs its inputs at frames 598-600.
- `3dssettings.h/.cpp`, `3dsmain.cpp`: settings, `.3d` keys (`SHADOW`,
  `SHADOWX`, `SHADOWY`, `SHADOWCOLOR`, `SHADOWSMALL`), editor block.

## Limits
Silhouette, not projected; games that draw their own shadow get two; a
sprite hidden behind a BG tile the gauges put behind it still shows its
shadow on that tile; shadows between rows of one plane are impossible;
Mode 7 casts nothing.
