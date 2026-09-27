# Drop shadow per depth row (issue #77, v1)

A marked layer/priority row casts a silhouette onto the layer behind it
in depth (Mega Man X3: X's outline on the wall). Glow and per-sprite
marks stay out of v1 (the per-sprite signature machinery of #76 is gone).

## Model
- 12 rows: BG1..BG4 × priority 0/1, Sprites priority 0..3. A profile
  holds a 12-bit `ShadowRows` mask plus `ShadowX`, `ShadowY` (-4..4 px,
  default 2, 2; one light per scene).
- The shadow of a row takes the parallax of the nearest row BEHIND it in
  the profile's depth gauges (largest depth smaller than its own, among
  rows the scene uses; its own depth when none), so it sits on that
  surface and the gap grows with the depth difference.
- One phase per target after everything else drew (color math and
  brightness included), through the game texture's ALPHA as a mask:
  nothing reads it past color math, and the screen blit uses vertex
  alpha. Per casting layer: (1) a full rect writes alpha 0; the plane
  right behind each casting tier (the nearest smaller Depth gauge plus
  every used row sharing it, one surface in 3D) writes alpha 0.45 where
  it has pixels, one pass per layer at the rows' own parallax, the Mode 7
  plane included (its own shader, the profile's perspective); (2) the
  caster writes alpha 0 over its whole body (every priority, zero blend
  factors under the alpha test); (3) per casting tier the silhouette
  blends its colour by the destination alpha (`DST_ALPHA /
  ONE_MINUS_DST_ALPHA`), offset by Shadow X through the tier parallax and
  Shadow Y through the spare `w` of `stereoIOD`, at the parallax of that
  tier's plane behind. On that plane, never on the body, never over a
  layer in front of the plane (unmarked). The caster's own pass and
  parallax are untouched.
- Rows further back than the plane get no shadow: a layer of the stereo
  profile, not of the SNES order, is the surface (Jorge).
- Stencil was tried and dropped: Azahar's stencil behaved differently
  from build to build with the texture-backed depth buffer, and the
  console flickered. Depth compares were tried first (GEQUAL passed
  everywhere, GREATER nowhere). Colour blending is what the blur uses and
  works on both.
- `ONE_MINUS_SRC_ALPHA` on the alpha channel did not erase on Azahar;
  zero factors under the NE_ZERO alpha test do.
- "Small Sprites Only" (SHADOWSMALL, default off): the sprite pass records
  each sprite's vertex span (`obj_spans.h`); passes (2) and (3) then draw
  only OBSEL-small sprites. Off by default: X in MMX3 is a large sprite.
- A new vertex uniform was tried and dropped: `add r0.y, uniform.w, r0.y`
  is mis-encoded by picasso (a 4 px shift); the y offset goes through a
  register copy of `stereoIOD`.
- Shadow Color: a six-entry table (black, dark gray, navy, brown, purple,
  white); saved as SHADOWCOLOR.
- Only in 3D (slider on), and off while Blur Auto's "Off under load" is
  engaged, so the Old 3DS pays nothing when it is already behind.

## Files
- `stereo_shadow.h`: row index, mask helpers, behind-depth lookup (pure,
  tested).
- `3dssettings.h/.cpp`: globals + profile fields, GPU3DS copy on apply.
- `3dsmain.cpp`: `.3d` keys `SHADOW=<mask>`, `SHADOWX`, `SHADOWY` (per
  profile and global); editor block "Shadows" with a checkbox per used
  row and two gauges; profile new/copy.
- `shader_tiles.v.pica`: `stereoIOD.w` added to y; `gpu3dsSetStereoParallax3`
  takes it as a fourth argument (0 everywhere else).
- `3dsimpl_gpu.cpp`: the shadow pass in the layer loop.

## Limits (v1)
Silhouette, not projected; games that draw their own shadow get two;
window clipping is ignored inside the phase. Cost per casting layer: a
rect, one mask pass per BG layer of the plane behind, the caster once
for the erase and once per casting tier; only in 3D.
