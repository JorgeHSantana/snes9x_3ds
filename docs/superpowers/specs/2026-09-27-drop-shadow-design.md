# Drop shadow and glow per depth row (issue #77, v4: depth EQUAL)

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
  Tiers sharing receiver, depth gap and planes draw in one pass. A
  marked row the scene does not draw casts nothing (skipped: its whole
  layer would otherwise be drawn for no pixel), and the editor keeps a
  marked row listed even when unused so it can be unmarked. A BG caster
  costs a full layer pass per receiver plane and eye; sprites are the
  cheap case.
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

## Glow (the bloom-like half, 2026-10-15)
- Per depth row (`GlowRows` mask), the marked rows' bright texels are
  drawn again, added to the frame, at the row's own parallax, after the
  shadow passes of the same phase (main target only: subtractive colour
  math on the sub screen would darken it). Works in 2D.
- Selection by brightness in the TexEnv: stage 3 alpha = tex.r x 76/255,
  stage 4 alpha = tex.g x 150/255 + previous (MULTIPLY_ADD), stage 5
  alpha = tex.b x 29/255 + previous = luminance; the alpha test keeps
  texels at or above `GlowThresh` x 16 (never 0: transparent texels have
  alpha 0 but luminance 0 too), and SRC_ALPHA / ONE blending adds colour
  x tint x gain x luminance. Stage 3 RGB = texel x (tint x gain), scale
  x2 for gains above 1. Destination alpha untouched.
- Halo (`GlowSoft`): 1, 5 (cross) or 9 (3x3) copies, the gain divided
  by the copies so the centre adds `GlowStrength`/4 of the colour and the
  rim less. Shadow Quality's load tier lowers the halo level and its fade
  multiplier scales the gain. Tint table (own colours, warm, cool, gold,
  white). Keys `GLOW`, `GLOWTHRESH`, `GLOWSTRENGTH`, `GLOWSOFT`,
  `GLOWCOLOR` per profile; editor block "Glow" after Shadows, rows
  spotlight their layer, gauges preview live.
- Gotcha: `gpu3dsDraw` applies the render state again, so a pass that
  overrides the alpha test must leave the applied alpha-test field as the
  draw expects it (a poison there put NE_ZERO back over the luminance
  test: both thresholds gave the same frame).
- Validation: `mmx3-glow` (threshold 8: 736 brighter px, 0 darker) and
  `mmx3-glow-thresh` (14: 357), against `mmx3-shadow-off`.
- The hardware check is Jorge's console: nothing in the repo used
  MULTIPLY_ADD, the R/G/B alpha operands or the x2 scale before.
- Per sprite (Jorge): by OBJ palette. A sprite tile's index carries its
  palette (`Palette << 10`, 1024 tiles per palette), so the tile vertex
  shader derives the palette from the tile (`floor(tile / 1024)`) and,
  while the `glowPal` uniform's enable flag is set, parks off-screen
  every tile whose palette is not in the 8-bit mask (bit = floor(mask /
  2^pal) mod 2, via ex2). No new vertex data; (255, 0) outside the glow
  pass is an exact no-op. `GLOWPAL` per profile (default all); the
  editor lists the palettes drawn last frame (`S9xObjPalettesUsedLastFrame`)
  and spotlights one by drawing the OBJ layer with its mask (prio 8 +
  palette in the highlight call). Sprite signatures (#76's machinery)
  stay out: a palette separates effects from heroes in practice.

## Output colour grade (2026-10-15)
- `screen_grade.h` (pure, tested): gauges -4..4 -> s = 1 + sat/4,
  g = 1 + con/8, b = bri/16, and the stage constants. Program in
  `gpu3dsSetScreenGrade`, set on the eye blit right after the render
  state applied (stage 0 is the blit's own replace) and cleared after
  the draw: stages 1-2 contrast (two paths: MODULATE then ADD_SIGNED for
  g <= 1; ADD_SIGNED then MODULATE with x2 scale for g > 1, brightness
  folded into the constant) while the alpha accumulates the luminance of
  the texture (76/150/29 with the R/G/B alpha operands and MULTIPLY_ADD);
  stage 3 passes RGB and writes it to the combiner buffer; stage 4
  INTERPOLATE(rgb, luma, s) for s <= 1 or max(rgb - luma, 0) for s > 1;
  stage 5 passthrough or MULTIPLY_ADD(that, s - 1, buffer); the final
  alpha is the blit's own (texture, or vertex alpha on the balanced
  filter's second pass). Game-global (a profile switch with hysteresis
  would make the colour jump), Display block of the options menu (the
  3D tab does not exist on a 2DS).
- The texture reaches the TexEnv with red and blue swapped relative to
  the screen (a dark red texel's luma came out with the 29/76 weights
  exchanged): the operand that reads "R" carries blue's weight
  (`SCREEN_GRADE_LUMA_*_OP_WEIGHT`, shared with the glow). Brightness is
  its own ADD_SIGNED in stage 5 when saturation is at most 1 (the
  interpolation towards the texture's luma would dilute it) and folded
  into the contrast constants above 1 (stage 5 is the saturation's).
- Azahar: neutral gauges byte-identical; sat +4 / con +2 raises the game
  area's mean chroma 49 -> 70; sat -4 / bri +2 turns it grey (chroma 0)
  at luma + 32, texel by texel.
- Occlusion (Jorge: "sees through walls"): every glow draw is per tier,
  depth-tested GEQUAL at the tier's own plane through the depth map
  (like the shadow's EQUAL): a layer in front holds a larger plane and
  covers the glow; the light leaks only onto what is behind the sprite.
- Spread (`GlowSpread` 0..3): the tier's glow pass is drawn once into
  `SNES_GLOW` (128x64 RGB565, its projection mapping the 512x256 SNES
  space onto it, cleared by a black rect in replace mode) and composited
  onto the main target as a tile-0 quad (raw texcoords) sampling it with
  bilinear magnification - the 4x upscale is the blur - once per tap
  (1 / 3 / 5: centre and diagonals one texel = 4 px away), each at
  1/taps, additive, depth-tested at the tier's plane. The halo copies
  are off while Spread is on. The target is allocated LAST and optional
  (`impl3dsAllocGlowTexture`): VRAM is 32 KB from full after the UI
  textures, and a mandatory 32 KB RGBA8 target made the boot fail on
  the second screen's background texture. The packed render state's
  target field grew to 4 bits for the new target (TARGET_UNSET = 8),
  which forced a re-layout: with devkitARM's byte-sized enums a bit
  field must not straddle a byte, or the union grows past 64 bits.
- Azahar: Spread 2 on MMX3: 1774 brighter px (halo path: 852), bbox
  7 px wider, none darker; neutral byte-identical.
- Vertex scratch: the clear rect and the composite quads use their own
  ring-buffered `SVertexList`s (`GlowScratch`, own VBO ids so the
  attribute-buffer cache rebinds). A borrowed slot of the scene VBOs
  "returned" with `count -= 2` is read by the GPU at the end of the
  frame with whatever was written there last: the black clear rect
  shared the depth floor's slot and made the floor write a non-zero
  depth on the console (artifacts with Spread on).
- Glow Source (`GlowSource`): "Whole screen" draws the frame so far (the
  main texture, or the right eye's) as one quad through the same
  luminance cut into the spread target and composites it back with no
  depth test - a post-process bloom at quarter res, HUD included; Spread
  is forced to at least 1. The palette rows show the sprite count per
  palette (`S9xObjPaletteCountLastFrame`).
