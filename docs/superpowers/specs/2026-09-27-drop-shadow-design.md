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
  renderer): for each layer with marked rows, its tiles as a silhouette
  (TexEnv stage 3: RGB = the chosen colour, alpha = texture alpha × 0.45),
  offset by ShadowX through the per-tier parallax and by ShadowY through
  the spare `w` of the `stereoIOD` uniform, at the "behind" parallax, with
  the blur's ghost blending, under the BG layers' depth rule (GEQUAL, so
  a BG in front of the caster covers the shadow); then the caster's base
  pass once more on top under the same rule, so its own body is never
  darkened and the SNES priority order holds. Unmarked tiers park
  off-screen through the per-tier parallax (the ghost-pass trick).
- Depth compares other than GEQUAL do not behave (measured: GEQUAL and
  LEQUAL pass everywhere, GREATER and LESS nowhere), so the shadow uses
  exactly the rule the BG layers use.
- A new vertex uniform was tried and dropped: registers past c9 of the
  tile shader did not reach the program (stereoIOD2 moved to c10 broke
  the picture), hence the spare component.
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
Silhouette, not projected; where the layer behind is transparent the
shadow shows on whatever is further back; games that draw their own
shadow get two. Cost:
two extra passes of each casting layer, only in 3D, off under Blur Auto's
"Off under load".
