# Effects: drop shadows, glow and colour grade

How the Effects tab's three blocks are drawn, what they cost and where
they are stored. Design history: `docs/superpowers/specs/2026-09-27-drop-shadow-design.md`
and the journal (`docs/journal/2026-09.md`, 2026-09-27 onwards).

## Drop shadows (issue #77)

* **When**: one phase per render target, after the BGs and before colour
  math (whose full-screen rects write z = 0 and wipe the depth buffer).
  By then every pixel's depth holds the plane of its visible surface:
  sprites write `(prio+1)*3`, BG tiles their mode's d0/d1 under GEQUAL,
  transparent and windowed texels never write.
* **How**: the caster's own geometry drawn once more at the receiver's
  plane (`C3D_DepthMap(true, 0, plane/32)`, depth func **EQUAL**, colour
  writes only), shifted by the receiver's parallax plus Shadow X/Y.
  A layer in front holds a larger plane, the caster's body its own, a hole
  in the receiver a smaller one: none of them passes. No mask, no erase.
* **Receiver**: the nearest used row at a smaller depth gauge (the gauges
  decide, not the SNES draw order); `GPU3DS.stereoBgPlane[]` carries each
  BG priority's plane out of the BG draw macros (`gfxhw.cpp`).
* **Soft Edge**: 4 or 9 copies at ±1 px, each with alpha
  `1-(1-a)^(1/N)` so the stack sums to the set opacity (`stereo_shadow.h`).
* **Quality** (`shadow_auto.h`): the Blur Auto load tier drops the Soft
  Edge level; "Auto, Off under load" fades the shadow over 30 frames.
* **Cost**: one draw per caster section per copy; 3-7 draws per frame in
  practice, 0.2-0.7 ms of CPU submission on an Old-mode console.

## Glow (issue #77)

* **Luminance in the TexEnv**: stages 3-5 accumulate `76 R + 150 G + 29 B`
  into alpha (per operand, because the texture reaches the TexEnv with R
  and B swapped), the alpha test cuts at the threshold, and the texel is
  added with `SRC_ALPHA, ONE` blending scaled by Strength and Tint
  (`gpu3dsSetGlowEnv`). The env must be set AFTER `gpu3dsApplyRenderState`
  (that resets the stages).
* **Threshold Relative** (`glow_relative.h`): the SNES palette's 90th
  percentile luma, tracked over 20 frames, scales the cut
  (`Threshold/16 x bright`) and the gain (`255/bright`, max 2x).
* **Spread**: the glow rendered into a 128x64 RGB565 VRAM target
  (`SNES_GLOW`, plus `SNES_GLOW_RIGHT` for the right eye) and composited
  back with 1/3/5 bilinear taps; `C3D_FrameSplit(0)` before sampling a
  target rendered in the same command list. The whole-screen source is a
  4-tap 4x4 box average of the eye's own frame (per eye: a single glow
  shifted into the other eye is right for one depth only).
* **Persistence**: the target is not cleared but multiplied by k/4
  (a rect with `ZERO, CONSTANT_COLOR` blending). The applied blend state
  must agree with the current one at the draw, or `gpu3dsDraw` re-applies
  DISABLED over it (see developer-gotchas).
* **Marked rows**: per row, depth-tested GEQUAL at the row's own plane
  (nothing glows through a wall); palettes gated in the vertex shader
  (`glowPal` uniform, tile parked at +4096 when excluded).
* **Quality** (`glow_auto.h`): the load tier drops Halo/Spread one level,
  crossfaded (gain to 0 over 20 frames, switch, back to 1).
* **Cost**: 5-14 draws per frame, 0.2-0.7 ms of CPU submission; the GPU
  side is fill-rate at a quarter resolution plus the composite taps.

## Colour grade

`screen_grade.h` + `gpu3dsSetScreenGrade`: TexEnv stages 1-5 of the eye
blit. Saturation interpolates toward the luma (or extrapolates past it via
the combiner buffer), contrast and brightness are a scale and an
ADD_SIGNED. Game-global, not per profile.

## Storage

Per profile in `stereo3d/<game>.3d`: `SHADOW` (row mask), `SHADOWX/Y`,
`SHADOWCOLOR`, `SHADOWALPHA`, `SHADOWFADE`, `SHADOWSOFT`, `SHADOWQUALITY`;
`GLOW` (row mask), `GLOWTHRESH`, `GLOWSTRENGTH`, `GLOWSOFT` (halo),
`GLOWCOLOR`, `GLOWPAL`, `GLOWSPREAD`, `GLOWSRC`, `GLOWPERSIST`,
`GLOWQUALITY`, `GLOWREL`. Game-global: `SHADOWS` (0/1), `GLOWMODE`
(0 off, 1 3D only, 2 always), `COLORSAT/CON/BRI`.

## Diagnostics

`[perf][frame]` carries `shadow-draws/frame`, `glow-draws/frame`, the
switches, `src/spread/halo/persist/rel/bright` and the CPU time per frame
of the core (and its PPU walk), the eyes' submission and the effects
phase. Send Screenshot (Emulation menu) posts both eyes as PNGs with the
effect settings in the comment title; `tools/github-screens/fetch.py`
downloads them. Azahar scenes: `tools/azahar-validate/scenes/mmx3-*`
(SBS builds composite both eyes side by side).
