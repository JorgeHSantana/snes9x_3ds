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
- One phase per target after the BG layers (sprites draw first in this
  renderer). Per marked row, three stencil passes over the stencil's free
  bits 0-1 (window masks live in 5-7): (1) every used row BEHIND the
  caster (depth gauge smaller than its own) marks bit 0 where it has
  pixels, one pass per layer at its rows' own parallax; (2) the caster
  marks bit 1 over its own body (REPLACE, so bit 0 clears there); (3) the
  silhouette (TexEnv stage 3: RGB = the chosen colour, alpha = texture
  alpha × 0.45), offset by Shadow X through the tier parallax and Shadow Y
  through the spare `w` of `stereoIOD`, at the nearest behind row's
  parallax, draws only where (stencil & 3) == 1: on a surface behind the
  caster, never on its body, never over a layer in front (its pixels
  were not marked). The caster's own pass and parallax are untouched.
- Stencil ops need the depth stage enabled (test ALWAYS, no writes);
  writing the marks through the depth texture as a colour target (the
  window prepass's way) did not reach the stencil test mid-frame on
  Azahar. Windows are not honoured inside the phase. The eye pass's depth
  floor resets the stencil next frame.
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
window clipping is ignored inside the phase. Cost per casting row: one
mask pass per BG layer behind it, the caster twice; only in 3D.
