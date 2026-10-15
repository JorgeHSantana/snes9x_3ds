# Drop shadow per depth row (issue #77, v4: depth EQUAL)

A marked layer/priority row casts a silhouette onto the layer behind it
in the profile's depth gauges (Mega Man X3: X's outline on the wall).
Glow and per-sprite marks stay out (the per-sprite signature machinery
of #76 is gone).

## Model
- 12 rows: BG1..BG4 x priority 0/1, Sprites priority 0..3. A profile
  holds a 12-bit `ShadowRows` mask plus `ShadowX`, `ShadowY` (-4..4 px,
  default 2, 2; one light per scene), `ShadowColor`, `ShadowSmallOnly`,
  `ShadowAlpha` (1..7 eighths, default 4), `ShadowFade` (0/1).
- "Small Sprites Only" (a per-sprite vertex span table filtering by OBSEL
  size) was removed: the main screen reuses the sub screen's sprite
  vertices without recording spans (no shadow at all on games with a sub
  screen), the per-scanline sprite path records nothing while spans from
  another segment survive (partial shadows), and every span drew with the
  first OBJ section's state. The caster is always the whole OBJ layer.
- Shadow Opacity sets the silhouette's alpha; Fade by Distance lightens
  it with the depth gap between caster and receiver in gauge units
  (full at 1, a quarter at 13 or more): a shadow dilutes as the surface
  moves away. Both are TexEnv constants, no extra pass.
- Soft Edge (`ShadowSoft` 0..2): Light = the silhouette drawn four times
  in a 2x2 px box, Full = nine times in a 3x3 box centred on the shadow,
  each copy at 1 - (1-a)^(1/copies), so the pixels every copy covers
  darken exactly like the plain shadow and the rim gets less - a box
  blur of the mask. Off by default.
- Shadow Quality (`ShadowQuality`, `shadow_auto.h`, default Auto): the
  Blur Auto controller's load verdict (Full / Light / Off, read whatever
  Blur Quality is set to) drives the shadow. Full: no optimisation. Auto:
  the Soft Edge level drops one step per load tier and climbs back with
  the controller's hysteresis; the shadow itself stays. Auto, Off under
  load: at the Off tier the whole shadow fades out over 30 frames (and
  back in the same way) - never a one-frame switch, which is what made
  the first Blur Auto gate flicker. The editor previews the shadow as
  set. The frame loop steps the controller once per emulated frame and
  publishes the level and the fade multiplier in `GPU3DSExt`.
- Stretch (scaling the silhouette) was built and removed: a scale needs
  an anchor, and the only one a frame offers is the OAM rectangle, which
  changes with every animation frame (per sprite: neighbours overlapped
  and parted; per cluster of touching sprites: the anchor jumped and
  characters whose sprites do not touch split into cuts). A smear along
  the light was rejected too (it multiplied the shadow).
- The depth buffer already holds, per pixel, the plane of the visible
  surface by the end of the BG passes: sprites write `(prio+1)*3`, BG
  tiles their mode's d0/d1 (`gfxhw.cpp` depth tables, now mirrored in
  `GPU3DS.stereoBgPlane`) under GEQUAL, transparent texels and windowed
  pixels never write. The colour-math and brightness rects then write
  z = 0 over the whole screen - which is why the first measurement
  ("GEQUAL and LEQUAL pass everywhere") saw nothing: it ran after them.
- One phase per target, after the last BG and before colour math. Per
  casting tier, per receiver plane: the caster's own geometry (its
  sections) drawn once more with the depth
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
  `SHADOWX`, `SHADOWY`, `SHADOWCOLOR`, `SHADOWALPHA`, `SHADOWFADE`, `SHADOWSOFT`, `SHADOWQUALITY`), editor block; a Shadows
  checkbox spotlights its row on the top screen like a depth gauge.

## Limits
Silhouette, not projected; games that draw their own shadow get two; a
sprite hidden behind a BG tile the gauges put behind it still shows its
shadow on that tile; shadows between rows of one plane are impossible;
Mode 7 casts nothing.
