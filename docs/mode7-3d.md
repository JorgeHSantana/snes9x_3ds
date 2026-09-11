# Mode 7 in stereoscopic 3D

The full context of the Mode 7 work (September 2026): what ships, why it
is shaped the way it is, what was tried and dropped, how it is proven,
and what is still open. The day-by-day record is in
`docs/journal/2026-09.md`; the rendering mechanics in `rendering.md`.

## 1. What ships (nightly 2026-09-08, 373a846)

### Perspective (issue #62)
A Mode 7 plane is drawn as one scanline pair per row through the tile
shader. Each row already knows how many texels it walks (the A/B/C/D
matrix); that span is its distance. `gfxhw` queues the frame's rows,
takes the smallest span as the nearest row and stores
`w = 256 * spanNearest / span` in the scanline vertex. The shader
multiplies the row's stereo shift by `1 + k * (w/256 - 1)` (k = the
Perspective gauge 0..4 over 4; 5..8 add a near-row gain), so the plane
recedes scanline by scanline. **Anchor: the horizon sits on the screen
plane and the layer gauge (BG1) is the nearest row.** A top-down map has
a uniform span and stays flat by itself.

The right-hand vertex of a scanline is the geometry shader's marker
(any projected y < -1) and carries the row's depth nibble on top
(`-16384 + (y & 0xF00)`, never the alpha bits), so both ends decode the
same plane and take the same stereo tier: the row moves, never stretches.

### Effects by distance
With the profile switch on, fade and haze fog the plane row by row
towards the horizon (the vertex shader writes the row's fog into the
vertex colour; a TexEnv variant interpolates it) and the blur's ghost
offset grows with the row's distance. Nothing at the nearest row, the
full gauge at the horizon.

### Stable sprite depth

Sprites use the same model as rcmz: perspective is applied only to the
Mode 7 plane, while every sprite remains whole in one of the four fixed
SNES OBJ-priority depths. The retired ground-following experiment tried
both proximity grouping and independent OAM pieces; the former changed
membership during animation and wobbled, while the latter visibly pulled
composite sprites apart. OAM provides no reliable logical-character ID.

Legacy `GSPR`, `GLIFT` and `GROUNDX` profile data is still accepted so old
files remain readable, but it no longer affects rendering.

### Editor (3D Stereo tab)
No paragraphs in the tab; SELECT on any item opens its help. The Mode 7
block exists only while the game uses Mode 7 and reads, in order:
Effects by Distance, a note that sprites use stable OBJ priorities,
Perspective, Plane Depth (the BG1 Prio 0 gauge,
moved out of the Depth list), Priority Pixels (BG2 Prio 1, EXTBG games
only). Tools are rows with help.

### Blur Quality Auto
Starts in Light (one ghost per eye on opposite sides fuses into the same
smear Full's two ghosts give) and turns Full after three clean seconds;
the first promotion is not a relapse. A frame drop flips it back to
Light; the proof it needs doubles on quick relapses.

### EXTBG
Games whose Mode 7 has per-pixel priority (Tiny Toon, Contra III,
Castlevania IV) draw the plane as BG1 plus two BG2 passes; the low pass
is BG1's own pixels under BG1 and is skipped when BG1 draws the same
segment with the same window mask. **Super Mario Kart does not use
EXTBG** (measured: the branch never runs), so it gains nothing here.

## 2. Decisions and why

* **Anchor of the perspective**: the geometrically "correct" anchor
  (gauge = nearest row, horizon sinks past it, whatever the sign) shipped
  for one day (104d1ec) and was reverted: on the console the whole plane
  sat behind the gauge, the far rows left the comfortable fusion range
  and the depth read vanished. Perception beat geometry.
* **Ground Lift instead of Near/Far**: two free depths allowed a sprite
  behind the ground under it; interlocking with the plane removed the
  impossible configurations and made one gauge do the job.
* **Sprite list inline, not a sub-dialog**: the live spotlight needs the
  tab's idle preview, which dialogs do not run.
* **Character identity by position**: signatures (sheet row + palette)
  change with the animation; positions move a few px per frame.
* **Sprites on the ground draw sharp**: blur ghosts are not offset for
  them (v1 limit, see open items).

## 3. How it is proven

* **Unit tests** (`tests/test_mode7_persp.cpp`, `tests/test_ground_sprites.cpp`,
  `tests/test_blur_auto.cpp`): the encodings, the slew, the tracker
  (including the scenarios read off the console log: dust stealing a
  memory, an item's memory on the kart's spot, feet in the band below
  the plane), clustering, shadow adoption, the lift depths, the EXTBG
  predicate, the right-vertex marker with every alpha value.
* **Azahar harness** (`tools/azahar-validate/`, scenes `smk-race-*` from
  Jorge's race state): side-by-side disparity per row band on a
  `-DPROBE_SBS -DPROBE_FORCE_SLIDER` build. Flat plane 6 px, Perspective
  8 growing 4.5 px from the horizon down, sprites 0 px with the switch
  off and 3 px with Lift 3. `--fbdump` (a `-DPROBE_FBDUMP` build) makes
  the emulator save its own top screen, so the capture works with the
  Mac's display locked.
* **Field probe**: an empty `sd:/3ds/snes9x_3ds/groundprobe.txt` created
  *before* launching, plus the session log, makes the emulator log a
  `[groundprobe]` line every 8th frame with every character's feet
  position, target row and row in use. It writes to the session log, not
  to the txt. This log, not deduction, found the two real causes of the
  drift/hit snap.
* **Console**: Jorge validates on a New 3DS; the Old 3DS is the
  performance gate (F-Zero fine, Mario Kart heavy by construction: DSP-1
  on the CPU, two planes per frame for the split screen).

## 4. Lessons written down the hard way

* A boot without errors is not a visual proof: the striped plane
  (102a86a) reached the console that way. Every rendering change ships
  with a capture looked at or a measured scene.
* Ask for the console log before the third fix. Five fixes by deduction
  did nothing for the drift snap; the first log found both causes in
  minutes.
* Probe the frame, not the call: a "per frame" log that fired several
  times a frame revealed the sprite data being computed per screen
  segment.

## 5. Open items

* Confirm on the console that dirt, drift and wall hits are smooth with
  373a846 (and send the `[groundprobe]` log if not).
* Blur ghosts offset for sprites on the ground (they draw sharp now).
* Old 3DS: Mario Kart 2D vs 3D frame times, to know whether anything of
  the 3D path is worth cutting.
* Issue #77: drop shadow / glow per row and per sprite, reusing the
  ghost-pass and slot-table machinery (design agreed, not started).
* Issue #74 (post-process blur) and #75 (UI design pass, partly done by
  the tab declutter) stay parked.
