# Changelog
Notable changes to this project will be documented in this file.

## Unreleased (nightly)
### Fixed
- **The console froze whole, mostly on pausing or right after an effect tier changed**: a frame that splits its command list (the layer scratch image, glow) left the first piece in the CPU cache, and the GPU ran what the memory still held. The stall watchdog's first report from the console showed the main thread waiting for a GPU that never finished. The split now has the piece flushed.
### Diagnostics
- **The log of a session that froze survives**: the session log is rewritten at every start, so a freeze that needed the power button left nothing to send. A log that does not end with its close line is now kept at the next start (`debug_<version>_unclean.log`, one file) and **Send Log to GitHub** carries its last 12 KB ahead of the running session's, then deletes it. Sleep, wake and each step of the way into the pause menu write a `[mark]` line straight to the card, and the LCD rate changes log the values they write, so the last line names where it stopped.
- **Stall watchdog**: a freeze on pausing left no mark, because the marks reach the card through the log's writer thread. A thread above the main one now watches the frame counter; when no frame ends for 2 s it appends one line to `debug_<version>_stall.log` naming the step the main thread, the audio mixer, the MSU-1 read-ahead and the log writer were each in, one more a second later (the step counters tell a blocked thread from a running one) and one when frames return. **Send Log to GitHub** carries the file first and deletes it. Off in the menus and during sleep.
- **Perf line**: `drawR=`, `scratch=` and `split=` break the eyes' layer submission down (the right eye's pass, deciding and building the layer scratch images, the mid-frame command list splits).
### Performance
- **Shadow and glow draws stop copying indices**: every shadow caster and glow source was drawn through a list of indices copied on the spot and flushed from the CPU cache with a system call, per draw and per eye (about 13 calls a drawn frame in the Mega Man X3 Zero Project dam). They now point into one fixed list written once at start. Same picture; the console numbers are pending.
- **Built with -O2**: on the console it ran better than -O3. `make 3dsx CORE_OPT=-Os` builds the SNES core at another level for comparisons.
- **GPU Cost Probe**: two more steps (60 s in all) that split what a tile costs the GPU on plain stereo: "tiles without fill" (the tile and sprite draws scissored to one pixel) and "tiles vertex shader only" (the same draws without the geometry shader). Fill, geometry shader and vertex shader come out by subtraction; for those 10 s. A last step (65 s in all) switches the layer scratch images off
### Fixes
- **Blurred layers with split depths**: every pass of a tiled layer (each eye's base pass and its blur ghosts) wrote its culled index list into the same range of the index buffer, and the GPU only reads it when the frame's command list runs, so all of them drew with the last pass's list. Each pass now has its own range. No difference in the scenes tested (the lists agreed there); it could drop or repeat tiles at the screen edge where the passes' parallax differed
### Features
- **Layer scratch image**: a background that the frame draws many times (main and sub screen, two eyes, blur ghosts) is drawn once into a 512x256 image and every pass becomes one quad per priority plane, where the cost model says the vertices saved pay for the image's fill. The images live inside the Mode 7 texture (no VRAM left for more), so it stays out of Mode 7 screens. New 3DS, MMX3 ZP's heavy phase with blur, shadows and glow: 21,628 vertices a frame down to 8,528, frame load 15.4 ms to 13.1 ms, skipped draws 68 of 240 down to 26. Azahar bit-identical on five scenes with every candidate forced through the images. `-DLAYER_SCRATCH_OFF` builds without it

## Stable v2.7 (2026-11-10)
### Fixes
- **Mode 7 Effects by Distance**: Fade and Haze on a Mode 7 plane now really fog the rows towards the horizon. The amount was written at 1/248 of its value since the feature's first build (the geometry shader scales every per-vertex tint by that), so the plane never fogged; the nearest row stays untouched as before
### Features
- **Partial end-of-frame flush**: the frame's draws read only what the CPU wrote this frame (the vertex lists' used halves, the layer and shadow index buffers, the glow scratch, the tile atlas rows touched), so only those are flushed from the data cache. citro3d's default flushed the whole 31 MB linear heap every frame: 0.3 ms on a New 3DS, up to 1.1 ms in Old mode (`flush=` in the perf line). `-DGPU_FULL_FLUSH` restores the old flush for diagnosis
- **Performance section** (Settings tab, top of the game-specific settings): Frameskip, Frame Sync and Skip Idle Loops moved here, plus a new **Effects Quality** picker that sets the shadow, glow and blur Quality controllers at once (Full / Auto / Auto, Off under load / Light; Mixed when they differ) and the GPU Cost Probe, moved from Emulator > Others
- **BG occlusion cull**: a background whose tiles are all hidden behind another background's full-screen wall of opaque tiles (no transparent texel, no window, every priority above) is no longer drawn there - in 2D the whole layer, in 3D everything but the strip at the screen edge the two layers' parallax leaves bare. MMX3 ZP's heavy phase draws two such full backgrounds, one entirely behind the other: 957 quads of 3,365 per eye gone. Exact by construction (Azahar bit-identical on nine scenes); the GPU Cost Probe has a "no occlusion cull" step and the perf line a `culled=` count. The occlusion cull is per tile priority (Mode 1's BG3 priority bit keeps its priority-1 tiles on top).
- **BG window cull**: a windowed background's tiles that the window stencil rejects on every pixel are not drawn either, in the base and the blur ghost passes. MMX3 ZP keeps a full-screen BG3 on the high priority and windows it down to the water: 744 of its 928 tiles per pass per eye were drawn to be stenciled away. Exact by construction (the same window sections and stencil test the GPU gets); Azahar bit-identical.
- **Tile vertex shader trimmed**: the Mode 7 perspective block, the glow-by-palette block and the stereo tier cascade (18 + 15 instructions of ~110) now run only when their uniforms say so - the cascade only when a layer's four tiers actually differ (never in 2D). The probe put a plain 2D frame of MMX3 at 5.5 ms of GPU with Enhanced Resolution off, i.e. vertex-bound: every instruction here is paid ~6000 times per layer pass. The perf line and the probe now also count the draw calls and vertices per frame (`draws=`, `verts=`), the probe lists every draw of one baseline and one 3D-off frame (`[probe][draw]`) and counts each BG's fully opaque tiles (`opaque-tiles=`), and the tile geometry shader drops three redundant projection products per quad.
- **`gpu=` in the perf line and the probe**: the PICA's actual busy time per drawn frame (first command-list submission to the last P3D event, stamped by a thread waiting on it). `gpuwait` only showed what spilled past the CPU's frame; in New mode the CPU is done in 4 ms, so a 12 ms GPU frame read as zero wait.
- **Blur ghosts in one pass pair**: the tile shader also carries each tier's ghost alpha (colour red while a ghost pass draws), so a layer's two blur ghosts draw once each whatever the depths, instead of once per depth group. GPU Cost Probe, MMX3 ZP heavy phase with the Auto tiers held at Full: the ghost groups were the remaining 11 ms of GPU wait. The probe now has separate "no blur" and "no fade/haze" steps (9 steps, 45 s) and the Auto controllers stay quiet in the log while it runs.
- **Fade / Haze in one pass**: the tile shader now carries each priority tier's fade/haze tint amount (a 4-float uniform, like the parallax), so a layer with split depths draws its base pass ONCE instead of once per depth group. Jorge's GPU Cost Probe on MMX3 Zero Project's heavy phase: the per-group redraws were the whole 12.6 ms GPU wait; only the Blur ghosts still add passes.
- **GPU Cost Probe** (Emulator tab, Others, with logging on): switches one piece of the render off every 5 s - colour grade, glow, shadows, depth effects, the right eye's pass, 3D - and logs each step's GPU wait, so a single Send Log says where the PICA's time goes.
- **Skip Idle Loops** (Emulator tab, per game, default on, `IdleLoopSkip`,
  game cfg v2.0): the CPU jumps to the next event when it sits in a loop
  that only polls `$4210`/`$4212`, the APU ports, the SuperFX status or a
  RAM flag, and the SPC700 does the same on its port and timer polls (the
  Snes9x 1.43 idle detection, re-done). Off for SA-1 games and the titles
  the core already blocks. Disable it for a game that hangs, stutters or
  loses its music. In Azahar: Zelda ALTTP 347 CPU + 532 APU skips per
  frame, core time -30%; MMX3 0 (its driver never idles).
- **Auto quality reacts to the GPU.** Blur, Shadow and Glow Auto only
  dropped a tier when a frame was skipped; a New 3DS finishes the CPU work
  in 5 ms and then waits up to 9 ms for the GPU (Jorge's log: 100 of 300
  frames over budget), so they sat at Full. A frame whose GPU wait exceeds
  20% of the budget now counts as a skip (`gpu_pressure.h`, `[blur] auto ->
  light (gpu)` in the log).
### Fixes
- **Menu speed**: the generic menu loop compared the raw 3D slider every
  frame (the v2.6 fix only covered the 3D tab's live preview), so with the
  slider off its detent the jitter re-rendered the paused game screen on
  most frames in every tab. Now the same 1/32 hysteresis everywhere; the
  "Press START to resume" caption is not re-rasterized when unchanged; the
  log is flushed once on menu entry instead of per frame; a ROM without a
  thumbnail no longer re-searches the index on every redraw; the 3D/Effects
  live preview draws the scene once instead of twice per cursor move.
### Performance
- The right-eye pass is skipped when every layer sits at depth 0 and
  shadows are off (both eyes read the same texture).
- MSU-1: the data-track SD read no longer runs under the mixer's lock, so
  FMV games don't stall the emulation thread behind it.
- HBLANK_END walks the deferred $212C-$2131 list only on scanlines that
  saw a write; the SuperFX call is gated at the caller.
- Release build: `-DNDEBUG` (dr_flac asserts off) and `-fno-math-errno`.
- OAM and OBSEL writes flush with per-layer deferral: only the sprite
  layer renders the section, the BGs catch up at the next full flush
  (Mario Kart had 3 of its 8 sections per frame from OAM).
- The tile cache keeps the atlas position and the palette stamp in one
  8-byte entry per (tile, palette): one cache line per tile lookup
  instead of two (the 512 KB `VRAMPaletteFrame` table is gone).
- Sprites take the whole-sprite path from 4-line sections (was 16).
- The mixer writes stereo straight into the NDSP buffer in one pass.
- Dialogs open and close in 4 steps instead of 8.
- Perf line: `flush=` (the end-of-frame cache flush), `gpuwait=`,
  `idle=` and `aidle=` (CPU / SPC700 skips per frame).


## Stable v2.6 (2026-11-04)
### Features
- **Glow Threshold Mode: Relative** (default for new profiles, `GLOWREL`):
  the cut is Threshold/16 of the scene's own bright - the SNES palette's
  90th-percentile luma, read on the CPU each frame and tracked over 20
  frames - and weak lights add up to 2x more, so the glow lives in dark
  scenes instead of vanishing (Jorge). Absolute keeps the old fixed cut.
- **Effects tab**: the full scene-profile block (New / Rename / Delete /
  Capture / Release This Screen) is there too, shared with 3D Stereo; the
  Shadows and Glow switches are named "Enabled"; **Glow Quality** Full /
  Auto - in Auto the Halo and the Spread drop one level per load tier with
  a smooth crossfade (`GLOWQUALITY`, per profile). New defaults: shadows
  off, offset 3/-1, opacity 3; glow off, threshold 12, strength 2, spread 1,
  halo Cross, persistence 1, quality Auto; colour saturation 1, brightness -1.
- **Glow Source defaults to "Whole screen"** (new games and new profiles).
### Fixes
- **MSU-1: the music no longer restarts when a sound effect plays.** A
  control-register write of "play" while the track was already playing
  rewound it to the start; per the MSU-1 spec only a track load rewinds,
  the write just sets the play and repeat bits (field log 2026-10-16).
- **Shadows and Glow switches** at the head of each Effects block, saved
  with the game (`SHADOWS`, `GLOWMODE`): Shadows Off/On; Glow Off, 3D only
  (only while the 3D slider is up) or Always. Off hides the block's options.
- **Glow Persistence fixed**: it never did anything (the decay rect's blend
  was undone by the draw's own state re-apply), and the right eye now has
  its own glow target so the trail decays per eye instead of per pass.
- The `[perf][frame]` log line also says whether 3D was on, the edge mode,
  the switches, the glow source/spread/halo/persistence, and the CPU time
  per frame of the core (and its PPU layer walk), the eyes' layer
  submission and the shadow/glow phase.
* Send Screenshot to GitHub (Emulation tab, with the log and crash dump
  items): the paused top screen, left and right eye, redrawn undimmed
  and posted as PNG (base64) comments on the issue named in
  `github.env`, split into parts when a PNG passes GitHub's comment
  limit. `tools/github-screens/fetch.py` reassembles them into files.
* Effects tab (Jorge): Shadows, Glow and Color move out of the 3D Stereo
  tab and the Display block into their own tab, beside 3D Stereo, with
  the same scene profile in edit (picker at the top) and the live top-
  screen preview. On a 2DS the tab exists with Glow and Color. With Glow
  Source on "Whole screen" the row and palette checkboxes are hidden.
  The cursor comes back to the row it was on after a rebuild (rows with
  the same name in several blocks no longer send it to the first one).
  The file tab is "Games". Effects labels drop the block's name
  ("Offset X", "Threshold"...). Glow "Persistence" keeps part of the
  last frame's glow in the spread target: on the console the two eyes
  are drawn on alternate frames, so a flash blinking every frame glowed
  in one eye only (Jorge's eye strain); with persistence both eyes glow
  alike, at the price of a short fading trail on fast motion.
* Output colour: Saturation, Contrast and Brightness gauges (-4..4) in
  the Display block of the options menu, applied in the TexEnv stages of
  the screen blit itself - no extra pass, so free on an Old 3DS. Saved
  per game in the `.3d` globals (`COLORSAT`, `COLORCON`, `COLORBRI`),
  `default.3d` for games without one. Neutral gauges leave the frame
  byte-identical.
* Glow per depth row (issue #77, the bloom-like half): in the 3D Stereo
  tab, a "Glow" block lets any Depth row's bright texels glow - drawn
  again, added to the frame in their own colours (or a tint) with a
  small halo, selected by luminance in the TexEnv (Glow Threshold), with
  Glow Strength and Glow Halo (Off / Cross / Box); Shadow Quality lowers
  the halo under load. Works in 2D too. "Palette N" checkboxes pick
  which sprite palettes glow (the charged shot's, not the hero's), with
  the top screen showing only that palette's sprites while the cursor
  is on the row. A Glow Spread gauge lets the light leak: the glow is
  drawn at a quarter of the resolution into a small VRAM target and
  blurred back over the frame with bilinear taps (~4-8 px). The glow is
  depth-tested at the sprite's own plane, so nothing glows through a wall
  in front; the light leaks only onto what is behind. A Glow Source
  picker switches to "Whole screen": everything in the frame above the
  threshold glows (HUD included), a post-process bloom through the same
  quarter-res target, rendered per eye from that eye's own frame (a
  4x4 box average, so the screen scrolling does not make isolated bright
  pixels blink). Glow Persistence smooths the glow over frames, which
  also tames pixels that flicker across the threshold while scrolling.
  Palette rows show how many sprites use each.
  Saved per profile (`GLOW`, `GLOWTHRESH`, `GLOWSTRENGTH`, `GLOWSOFT`,
  `GLOWCOLOR`, `GLOWPAL`, `GLOWSPREAD`, `GLOWSRC`).


## Stable v2.5 (2026-10-15)
### Features
* Drop shadows per depth row (issue #77): in the 3D Stereo tab, a
  "Shadows" block lets any Depth row (BG priority or sprite priority)
  cast a silhouette of itself onto the layer behind it in depth, with
  Shadow X / Y offsets, a Shadow Opacity gauge, "Fade by Distance" (the
  shadow lightens with the depth gap to the row behind), a Soft Edge
  picker (Light: a 2x2 px box, four passes; Full: 3x3, nine), a Shadow
  Quality picker (Full; Auto: the Soft Edge steps down with the Blur
  Auto load verdict and back; Auto, Off under load: the shadow also
  fades out over half a second under persistent load, and back), a Shadow Color
  picker (black, dark gray, navy, brown, purple, white), saved per
  profile in the `.3d` (`SHADOW`, `SHADOWX`, `SHADOWY`, `SHADOWCOLOR`,
  `SHADOWALPHA`, `SHADOWFADE`, `SHADOWSOFT`, `SHADOWQUALITY`).
  Moving the cursor onto a Shadows
  row spotlights that row on the top screen, like a depth gauge.
  The silhouette is the caster drawn once more at the receiver's depth
  plane with an EQUAL depth test, before colour math: it lands only where
  the layer behind (the nearest smaller Depth gauge, with every row
  sharing it; the Mode 7 plane included) is the visible surface, never
  on the caster's body, never over a layer in front, and the caster's own
  stereo position is untouched. One extra pass of the caster per receiver
  plane and eye. Only in 3D.


## Stable v2.4 (2026-09-27, 6b5abc4)

### Features
* Send Log / Send Crash Dump to GitHub (issue #80): two items in the
  Emulation tab under Enable Logging, shown only when
  `sd:/3ds/snes9x_3ds/github.env` holds a token and an issue's comments
  URL (see `github.env.example`; a fine-grained token limited to Issues
  on the repository). The current session log (its last 60 KB, cut at a
  line) or the newest Luma crash dump (`sd:/luma/dumps/arm11`, base64)
  is posted as a comment with the emulator version, console model and
  clock mode. Runs from the menu on a worker thread; the result dialog
  shows the comment's URL or the HTTP status.

### Fixes
* MSU-1: a track loaded right after a missing one (Zelda MSU's absent
  track 0) counted every fill as a stall while the read-ahead thread was
  still adopting it, and gave up after a second - silence for the rest
  of the track. The read-ahead now reports itself alive from the moment
  a playable track is posted. The adopt line logs the thread's latency
  (`wait`) and the decoder open time.
* Logging: the `[sig]` register-signature line is throttled (at most 8
  per 60 frames, a value flipping back and forth every frame is logged
  once, and a summary counts what was dropped). Zelda MSU toggled $210B
  every frame and produced 30 lines a second on the Old 3DS, with 7 audio
  underruns during the spam.

### Validation
* Profile build: `make ... EXTRA_DEFINES=-DPROFILE_LOG` turns every
  t3ds timer on and writes the 120-frame breakdown (core, sections,
  draw, GPU wait, flush) to the session log. A field build for the
  console, never a release.
* `[perf][frame]` every 5 s in the session log: the emulation thread's
  share of the frame budget (average and worst), frames over budget,
  draws the pacer skipped and the Blur Auto tier. The console number the
  optimization work was missing. The rewind slow-capture line names
  where the emulation thread's time went (pre, freeze, begin, thumb).
* Probe builds for the Azahar harness: `-DPROBE_HOLD_DOWN` holds D-pad
  Down for frames 120-599, `-DPROBE_FBDUMP_EVERY=N` dumps every Nth frame
  from 120 to 720, `-DPROBE_INIDISP_LOG` logs $2100 writes, black
  sections, segments, window registers and CGRAM per frame,
  `-DPROBE_NO_MSU_BLANK_HACK` turns the MSU-1 forced-blank suppression
  off. Scene `alttp-transition` (Jorge's Zelda MSU savestate).

### Optimizations
* Session log off the emulation thread (issue #59, source 1): lines go
  to a two-chunk spool and a lowest-priority writer thread on the
  mixer's core puts them on the SD; the emulation thread never touches
  the card (the previous buffer flushed synchronously when full: a 13 ms
  stall inside a rewind capture on the Old 3DS). If the writer falls
  behind, lines are dropped and counted, never waited for. The crash
  tail is unchanged: menu, exit and unload still flush synchronously.
* Rewind load reads the ring slot in place (`snap_reader.h`): the
  unfreeze path takes a small reader over a file or a memory block
  instead of `fmemopen`, which allocated a FILE and its buffer on every
  rewind step (issue #78).
* DMA into VRAM (issue #79, item 3): a linear word upload whose bytes VRAM
  already holds no longer forces a section render nor walks the per-byte
  write path; only the addresses advance. Zelda's area loader re-sends
  ~1400 such transfers a second (their cost fell by a third), Mario Kart
  ~80. Frame dumps byte-identical on Mario Kart, MMX3 and the Zelda
  transition.
* Mode 7 char index (issue #79, item 1): when a char's pixels or palette
  change, the map positions showing it were found by walking all 16384
  tilemap entries, once per frame with any dirty char (Mario Kart: every
  frame, for ~24 chars on the map). A reverse index char -> positions,
  relinked on each tilemap write and rebuilt lazily from VRAM after a
  reset, savestate or rewind, replaces the walk (`m7map_index.h`, 64 KiB).
  Mode 7 preparation -55% in Mario Kart; frame dumps byte-identical on
  the race, boot, MMX3 and Zelda scenes.
* Sprite line lists (issue #78, renderer): S9xSetupOBJ rebuilt all 240
  scanlines' sprite lists on every OBJ change; Mario Kart changes the
  table four times a frame (OBSEL and the OAM upload, once per split
  half) and paid four full passes, 40% of its section-render time. The
  lists are now built per line range on demand, each section building
  the lines it draws (`obj_lines.h`, oracle-tested against the original
  algorithm). Mario Kart -46% on that phase; frame dumps byte-identical
  on the three scenes. The FirstSprite+Y rotation keeps the full build.
  (Nightly a568eee drew the Nintendo logo at the top of Mario Kart's
  boot screen: after a mid-frame OBJ change the lines above it kept the
  old table into the next frame. The valid range is now one contiguous
  run, rebuilt as the frame reaches it; the boot is a harness scene,
  `smk-boot`, and pixel-identical to Stable.)
* OAM DMA (issue #79, item 3): the frame's OAM upload walks the low table
  by words, comparing each source word against OAM and applying only the
  changed ones with the register's own update (rotation, the high table
  and odd tails keep the byte path). Per-byte cost -46% in Mario Kart
  (two mid-frame uploads a frame), -61% in MMX3. Frame dumps
  byte-identical on the three scenes.
* Same-scanline flushes (every changed OAM word of a mid-frame OAM DMA,
  ~270 per DMA in Mario Kart) check a cached "any layer deferred" flag
  instead of calling the drain.
* Tile conversion (issue #79, item 6): the per-byte "is this bitplane
  byte zero" branches are gone; the tables map zero to zero, so the
  output is identical (Azahar byte-exact) with 16 fewer branches per
  8-bpp tile line.

## Stable v2.3 (2026-09-26, 3ae0954)

### Fixes
* Crash entering a folder with more than 50 games while its list was
  still empty (first build or a stale-cache rescan at boot): the
  "(caching...)" indicator indexed the empty item list. Data abort at
  NULL+8, from Jorge's Luma dump (Old 3DS, the Zelda MSU folder).
* Preserve pending SRAM changes on write/close failure and restore the previous
  audio-silence state. File savestates also report buffered write failures.
* Ignore hidden Mode 7 distance-effect settings in Layer mode.
* Make clean release builds require the same patched citro3d as local builds.
* Move the remaining inline option explanations in Emulator, Settings and
  Controls into SELECT help, including savestates, screenshots, logging,
  SRAM, palette redraw reduction and Circle Pad hotkeys. Status rows remain visible.

### Optimizations
* Rewind captures skip the savestate format's byte-by-byte packing: the
  in-memory ring stores the core's struct fields as they are (same field
  order, no big-endian conversion), which only exists so a .frz file is
  portable. Savestate files are unchanged; a round-trip self-check runs
  in the validation probe.
* **Rewind capture costs the game less** (issue #59, source 2: 19-20 ms
  per capture on the Old 3DS, over a frame): the delta encode - a
  compare and copy over the whole state, ~40% of the capture - now runs
  on a background thread and the entry lands on the next frame; and the
  in-memory ring freezes only the SRAM the cart has instead of the
  format's fixed 128 KiB (a cart without SRAM skips 128 KiB of copy and
  compare per capture). Savestate files are unchanged.
* **SRAM autosave no longer freezes the game** (issue #59: 225-259 ms per
  periodic save on the Old 3DS): the emulation thread copies the SRAM
  (at most 128 KiB) and a background thread writes the file. One write
  in flight at a time; a refused or failed write keeps the SRAM dirty so
  the timer retries. Pause, sleep, ROM unload and exit still save
  synchronously, after the background write lands.
* Session log: one 32 KiB buffer drained once a second during gameplay
  (issue #59: per-line flushes were a measured stutter on the Old 3DS),
  flushed on every menu iteration and explicitly before exit, ROM unload,
  the updater and after each autosave - the moments a crash report needs
  the tail of.
* Reuse a bounded serialization workspace instead of allocating temporary
  blocks for each savestate/rewind field group. Save format is unchanged.
* Reuse sprite scanline lists for appearance-only OAM changes, preserving
  redraw timing and geometry/rotation invalidation.
* Mode 7: the nearest-row reference is picked in squared space, one
  square root per frame instead of one per row.
* These are the first changes for #78/#79, not completion of every proposed
  experiment. Hardware FPS/latency improvements remain to be measured.

### Features
* **Blur Quality "Auto, Off under load"**: a fourth mode that, when Light
  still drops frames for two seconds, switches the blur off until a few
  clean seconds pass (Jorge: Mega Man X3's water stage on an Old 3DS).
* **Rewind Capture Patience "Idle frames only"**: a capture is never
  forced, so a heavy stretch has no capture hiccup at all and a thinner
  history instead.
* **Sprites keep their OBJ priority depths over Mode 7**: the
  sprites-follow-the-ground model of the 2026-09-08 nightlies was retired
  (the kart's depth still snapped on drifts and wall hits, and OAM gives
  no reliable character identity); its code, `.3d` keys (`GSPR`, `GLIFT`,
  `GROUNDX`) and editor rows are gone. Old files still load. The "Depth
  Mode: Layer / Direct" picker of the 2026-09-12 nightlies is gone too:
  Layer was Perspective 0 under another name (and left the edge crop
  sized for the Perspective gain it ignored); `M7MODE=0` reads as
  Perspective 0.
* **Blur Quality Auto starts in Light** and turns Full after the run of
  clean seconds proves the game holds its rate (the two eyes fuse one
  ghost each into the same smear Full gives); an Old 3DS that never
  holds it never pays for Full. A skip right after that first Full is
  not treated as a relapse.
* **EXTBG in two passes instead of three** (Mode 7 games with per-pixel
  priority: Tiny Toon Adventures, Contra III, Super Castlevania IV):
  BG2's low-priority pass is BG1's own pixels under BG1, so it is skipped
  whenever BG1 draws on the same screen segment with the same window
  mask - one full-screen fill of the Mode 7 texture less per frame.
  `[m7] extbg: BG2 low pass ...` in the log tells whether it applied.
  Super Mario Kart does not use EXTBG (measured), so it gains nothing here.
* **Mode 7 perspective** (issue #62): in 3D, the ground plane of Mode 7
  screens recedes scanline by scanline (each row shifts by its own
  distance) instead of moving as a flat wall. Automatic on Mode 7 rows;
  a top-down or rotating map stays flat by itself. 3D Stereo tab > Mode 7
  > Perspective (0 flat, 4 full perspective, 5-8 push the near rows further
  out; per game/profile, `M7PERSP` in the .3d).
* Mode 7 with EXTBG: the plane is BG1 and the priority pixels are BG2
  Prio 1 (Mario Kart puts the road there); which is which depends on the
  game, so both stay as absolute gauges in the Depth list, which shows the
  rows the paused screen really draws.
* **Mode 7 effects by distance**: with Effects by Distance on (default),
  fade and haze fog the plane row by row towards the horizon and blur
  widens with distance, instead of one tint and one smear for the whole
  plane (`M7FX` in the .3d).

### Fixes
* Mode 7 ground sprites no longer wobble when an animation alternates the
  hardware sprite size: a one-frame size change moves the inferred feet and
  used to make the depth slew reverse every frame. Direction changes now need
  two consecutive rendered frames of confirmation; sustained motion still
  follows the plane normally.
* Sprites on the Mode 7 ground glide to a new row instead of snapping:
  a hop, a drift or a wall hit moved the kart's depth at once (Jorge's
  reports). A character is now tracked by its screen position (an
  animation change no longer restarts it), the row it uses slides by at
  most 4/255 per frame, a character in the air above its shadow takes
  the shadow's row - the ground - and the memories are matched to the
  characters closest-first, so drift smoke or hit sparks appearing next
  to the kart cannot take its memory. And the real snap: on a skipped
  frame the plane draws nothing, and its empty row table put every sprite
  off the ground for the next frame - the rows of the last RENDERED frame
  are kept now. The sprite data is computed once per frame (it ran once
  per screen segment and re-registered every character as new, so the
  glide never happened), a character's feet are its largest sprite (the
  kart's body, not a puff of dust joining it a few px lower - dirt, drift
  and wall hits all spawn dust), and only a flat sprite below counts as a
  shadow. The console log then showed the real snap: a spinning or
  drifting kart's box dips into the band between the track and the map,
  where the plane has no rows, and fell off the ground for those frames -
  the nearest plane row above (up to 16 rows) is the ground there; and a
  passing item's memory on the kart's spot made the kart glide from the
  item's row - matching now weighs the row difference too. Ground Lift
  defaults to 0.
* Mode 7 scanlines move as one piece in 3D: the right-hand end of each
  row now carries the row's depth with the marker, so both ends pick the
  same stereo tier (before, a BG whose two priorities sat at different
  depths could stretch the row instead of shifting it). Nightly 102a86a
  drew the plane as stripes on the console - fixed the same day.
* Mode 7 perspective anchor stays as shipped (horizon on the screen plane,
  the layer gauge at the nearest row): one nightly (2026-09-07) sank the
  whole plane past the gauge instead, and the depth read vanished - the
  far rows left the comfortable fusion range (Jorge's report).
* 3D Stereo tab live preview (Jorge's report): changing Edge Cleanup
  (Trim -> Zoom), Blur Quality or Slider Response while paused now redraws
  both screen buffers, so the top screen no longer alternates between the
  old and the new crop; moving the slider on a depth gauge keeps the
  spotlight (the pause redraw used to paint over it); reopening the menu
  with the cursor on a gauge shows its spotlight again.


## Stable v2.2 (2026-09-06, a26c295)

### Features
* **Auto Save / Auto Load** (issue #72): Emulator tab > SAVESTATES (global,
  available before a game is loaded).
  Saves when the menu opens, on HOME, lid close and exit; loads that save
  when the game starts. Off by default; the per-game option still works.
* **Validation harness** (issue #69, for contributors): `tools/azahar-validate`
  boots a declared scene in Azahar with zero key presses and diffs the top
  screen per region against a golden or a second scene; a hardware
  checklist lists what only the console can verify. Every release now
  ships its `.elf` so field crash dumps can be symbolized.

### Fixes
* Blur Quality Light (and Auto under load) made blurred backgrounds
  wobble left and right: the single ghost alternated sides every frame.
  It now stays on one side per eye (opposite in the two eyes), steady in
  time.
* Blur Quality Auto no longer flips to Light during the first second
  after a ROM or state load.
* MSU-1 music was silent after the post-update resume: the parked state
  is now loaded inside a mixer drain window, like every other state load.
* Resuming an MSU-1 game after an update (or autoboot) left the file
  browser inside the pack folder; it now points at the pack's parent with
  the pack as the single entry, like a browser pick.
* Crash at exit right after a mid-game update (data abort): the file
  browser's background directory sweep, started when the unloaded game
  brought the Load Game tab back, was still reading the SD card when the
  process ended and the card was unmounted. The sweep now stops and is
  waited for on exit. (Diagnosed from the Luma crash dump.)
* Exit-path hardening found on the way: the session log is thread-safe
  (the MSU-1 read-ahead thread logs too), and a read-ahead thread that
  does not exit in time keeps its memory instead of being torn down.

## Stable v2.1 (2026-09-05, 254369f)

### Features
* **Per-priority stereo depth** (issue #60): each background's two tile
  priorities and each of the four sprite priorities get their own depth
  gauge, resolved in the vertex shader (no extra draw calls). Fade, haze
  and blur follow the priority, not the layer.
* **Live 3D editor** in the 3D Stereo tab (issue #61): focusing a gauge
  spotlights exactly its tiles on the game screen (the rest of the scene
  stays visible, dimmed), values move the paused frame in real time,
  holding X previews the full scene, Focus/Effects gauges preview live,
  and Enable/Disable Layers applies instantly.
* **Fill Priority Gaps** (issue #70, rcmz's idea): splitting a
  background's priorities uncovers a strip that belongs to neither; the
  farther priority is extended to paint it. On by default.
* **Slider Response** (issue #65): Discrete (whole-pixel shifts, layers
  move as one block - default) or Continuous (analog feel).
* **Blur Quality** (issue #71): Auto (default) draws the full two-ghost
  blur while the game holds its frame rate, switches to a one-ghost Light
  blur the moment a frame drops, and returns after a run of clean seconds
  that grows if it has to switch again soon. Full and Light can be forced.
  Light halves the blur's draw cost.
* **Unused rows dimmed / Hide Unused Layers**: layers and priorities the
  paused screen did not draw are dimmed in the 3D tab (still editable);
  the optional Hide Unused Layers removes them. Re-evaluated on every pause.
* **Shareable .3d files**: per-game 3D settings are now named after the
  ROM's internal title (`stereo3d/<title>.3d`), so a file matches the same
  game whatever it is called - regions and revisions share it. Existing
  filename-based files migrate automatically on first load.
* **Updating mid-game** (issues #66, #73): the session is parked in a
  savestate, the game is unloaded so the download has the machine to
  itself, and it reloads right where it was - in the same session, or on
  the next launch of the new build if you exit.
* 3D tab: Settings block sits above Tools; peek moved from Y to X (Y is
  the paging combo); download speed hidden at 100%.

### Fixes
* Blur ghosts were smearing every priority of a split layer; they are
  now masked to their own priority.
* Blur artifacts at some levels: ghost shifts round to whole pixels in
  both slider modes.
* Fade/haze/blur were keyed to priority 0 of a layer; effect groups now
  apply per priority (and merge by result, so Butoden-style split screens
  do not pay for no-op passes).
* Dialogs are sized to their wrapped text (Slider Response explanation,
  ROM loading, Update Installed).
* Update check could fail with an allocation error while a game was
  loaded: the network buffer is reserved at boot.
* Rebuilt menu tabs keep the cursor on the same row by identity.

## Stable v2.0 (2026-08-31, 21481ff)

### Features
* **Self-updater** (issue #64): the emulator updates itself from the menu.
  Settings tab > Updates: pick the channel (Stable or Nightly), check on
  demand, or enable "Check on Startup" to be offered new builds on boot.
  A 3DSX install replaces its own file on the SD card; a CIA install goes
  through the system installer - both take effect on the next launch.
  Downloads are verified (header + size) before anything is touched, and
  a failed update never harms the running version.
* **MSU-1 audio read-ahead** (issue #55): a producer thread decodes FLAC
  into a ~2s ring ahead of playback, so the mixer only copies samples.
  Loops play gapless - the loop seek that used to gap the audio for up to
  0.4s now runs off the hot path - and track changes stay clean.
* **Rewind reaches minutes, not seconds**: captures are stored as page
  deltas against periodic keyframes (issue #37) - the same memory now
  holds up to ~1.5min of history on New 3DS (0.5s steps) and ~1min on
  Old 3DS (2s steps), instead of 24s/4s of full snapshots.
* **Hold to rewind, tap for the timeline**: the Rewind hotkey combines
  both. Holding freezes the game, dims the screen and walks stored
  moments back under the button - real-time speed at first, then
  accelerating - with a blue corner badge showing how far you have gone.
  Releasing runs the configured countdown and play resumes. A short tap
  opens the timeline instead.
* **REWIND menu section** (Emulator tab): Recording on/off, Max History
  (30s / 1min / Maximum), Capture Patience (how long a due capture waits
  for an idle frame before pushing through) and Resume Countdown, plus
  the Open Timeline action in-game.
* **Timeline visual pass**: the menu's gray top bar with a TIMELINE
  title, sharp 150x90 preview, dot strip and elapsed label (zero units
  hidden: -2min 5s), blue highlights, battery + version in the bottom
  bar, and no more screen blinks entering, leaving or confirming.

### Fixes
* **Rewind captures no longer stutter the game** (issue #55): a capture
  used to wait for the audio mixer's lock - tens of ms while MSU-1 FLAC
  decodes, hundreds at a loop seek. It now retries next frame instead of
  waiting, so captures always land in a lock-free window. Slow captures
  and SRAM autosaves also log their duration for field diagnosis.
* **Old 3DS speed regression fixed** (issue #54, e.g. Super Mario Kart):
  two independent causes found by hardware profiling. The Mode 7
  sub-to-main vertex reuse lost in an upstream fix is restored with a
  per-section guard (~1.2ms/frame back), and the audio mixer's syscore
  budget is per-game again - 30% baseline, 45% only while an MSU-1 title
  is loaded (~1.6ms/frame back in every other game).
* Resuming from a rewound moment no longer freezes the screen for
  seconds with audio running (the modal's wall time read as frame-pacing
  debt and the loop skipped drawing to catch up).
* MSU-1 music no longer carries the present's track into a restored
  moment; timeline restores apply the real chip state.

### Fixes
* Rewind recording got cheap and controllable: a new **Rewind:
  Enabled/Disabled** setting (Emulator tab, enabled by default) governs
  the feature - disabling frees the snapshot ring (24MB) and stops all
  captures. Captures prefer frames with vsync headroom (forced at most
  every 2s), no longer disturb the live audio channels, and hold the
  mixer barrier - fixing the stutter, audio artifacts and swallowed
  notes reported on the first rewind build.

### Features
* **Rewind timeline**: press the Rewind hotkey (the hold gesture is gone)
  or pick Rewind in the Emulator menu to freeze the game and browse your
  history on a filmstrip of thumbnails with a dot strip and a "-12.5s"
  label; the game screen dims with a pause-style overlay - same bold
  lettering as "Press START to resume" (the fonts gained the missing bold
  glyphs). Y previews the exact frame; A previews AND asks Yes/No using
  the menu's real modal dialog, drawn over the dimmed timeline. Yes runs
  a "Resuming in 3..2..1" countdown on the game screen (step length in
  Emulator -> Rewind Countdown, or off) while the bottom screen returns
  to normal - the countdown cannot be cancelled. B goes back to where you
  came from (game or menu). Loading a savestate resets the recorded
  history. MSU-1 music stays paused while browsing and reseeks once on
  resume.
* 3DS Mode (Emulator tab, New 3DS only): "New 3DS" runs at 804 MHz with
  the L2 cache on (default - same behavior as before); "Old 3DS" drops to
  268 MHz without L2, previewing how a game would perform on that
  hardware. (#16)
* **Rewind**: about 24 seconds of history on New 3DS, ~4s on Old 3DS
  (snapshots every half second in RAM, adaptive). The Rewind hotkey
  (Controls tab) opens the timeline; so does the Emulator menu, no
  binding needed. (#12)

## Stable 2026-08-14 (a9813db)

### Features
* **Load ROMs from .zip archives**: the browser lists `.zip` files and the
  loader decompresses the first ROM inside (macOS junk entries are skipped).
  Saves and configs are keyed by the zip's basename, so they are shared with
  a loose copy of the same game. MSU-1 packs must stay unzipped.
* **Compressed MSU-1 audio (FLAC)**: when `<base>-N.pcm` is missing, the
  emulator plays `<base>-N.flac` instead (lossless, ~half the size, decoded
  in real time). The loop point comes from a `MSU1_LOOPPOINT=<samples>`
  metadata tag - see the README for the one-line ffmpeg conversion.
* **MSU-1 pack folders are listed as games**: a subfolder holding exactly one
  ROM plus MSU-1 files now appears in its parent directory as a single entry
  named after the folder, booting the ROM directly (no extra navigation hop).
* **Background directory refresh**: cached folders still open instantly, but
  a background sweep now spots content copied to the SD afterwards and
  refreshes the on-screen list automatically - no manual rescan needed.
  (Cache format bumped; the first visit after updating rescans once.)
* Opening the menu now points "Editing Profile" at the scene profile the
  current screen matches, instead of the last profile edited.
* MSU-1 pack entries are shown in blue in the file browser, and the ROM
  inside a pack folder may be a .zip (the pack itself stays a folder).
* Changing In-Frame Palette Changes now shows/hides the ADVANCED SETTINGS
  section (Reduce Layer Draws) immediately.
* Tools: "Scene Matcher Info" shows what the scene matcher sees (PPU
  signature, VRAM word, watch byte, matched profile) for debugging shared
  .3d files; "Set as Global Default" saves the game's look as the starting
  point for games without their own settings (default.3d - profiles never
  go global); "Copy 3D Settings From..." imports another game's look
  (profiles stay behind, the game's WATCH byte is kept).
* New "Tools" block closing the 3D section: "Reset 3D Settings" (deletes
  every profile and screen bind and restores factory values, behind a
  confirmation; the game's WATCH byte is kept) plus "Backup 3D Settings" /
  "Restore 3D Settings Backup" - snapshot the current 3D setup before
  experimenting and bring it back at any time. Backups live in the hidden
  .stereo3d-bak folder, keeping the shareable stereo3d/ folder clean.

## Stable 2026-08-13 (efb1096)

### Features
* **Scene Profiles (experimental)**: per-screen 3D configurations that switch
  automatically. Capture a screen from the pause menu ("Capture This Screen"),
  and the emulator learns a PPU-register fingerprint (auto-masking blinking
  bits) plus an optional WRAM game-mode byte (`WATCH=` in the .3d file) to
  recognize it. Profiles are managed in-menu (create as a copy, rename via the
  system keyboard, delete, release a screen) and persist in the shareable
  `.3d` files. Switches use a 30-frame hysteresis and smoothly interpolate
  everything: layer depths, Fade/Haze/Blur and the focus zone glide from the
  current values to the new profile's over 20 frames.
* "+ New Profile" opens the system keyboard right away to name the new
  profile (cancel keeps the automatic "Profile N" name).
* Edge Cleanup option (Off / Trim / Zoom, default Trim) hides the
  screen-edge columns disturbed by the per-layer parallax.
* Configurable focus zone (Focus Back/Front): layers inside stay untouched;
  Fade/Haze/Blur grow with the distance beyond it. Blur also applies to
  foreground layers (depth of field).
* Enhanced Resolution now doubles the parallax scale and refines the slider
  response (half-SNES-pixel steps); blur aura scales 1.875x there.

### Fixes
* The Smooth screen filter now also applies to the right-eye texture: with
  3D enabled the right eye composited with unfiltered (nearest) sampling,
  which read as "the filter turns off in 3D".
* Ghost blur no longer blacks out layers in color-math scenes (dialog boxes):
  ghost passes preserve the destination alpha used as the color-math mask.
* The right-eye texture is cleared on Enhanced Resolution changes (stale
  frame showed misscaled on the right eye after toggling the mode).
* Pause overlay and in-game notifications draw one layer in front of the
  highest configured pop-out, and the IOD base returned to the Standard 3px
  (menu/splash 3D was unintentionally running at maximum strength).
* Directory cache self-validates after 15 minutes, so ROMs/MSU packs copied
  while the console was off appear without a manual rescan.
* Menu spacing/color cleanup across all tabs; stereo settings reorganized
  (Scene Profiles, Layers, Depth, Focus, Effects).

## v1.61

### Features
* Added 3D depth strength setting and refined splash/background depth effects ([#65](https://github.com/matbo87/snes9x_3ds/issues/65)) ([c08c7f5](https://github.com/matbo87/snes9x_3ds/commit/c08c7f5c), [ef677f9](https://github.com/matbo87/snes9x_3ds/commit/ef677f9f))
* Added save-state screenshot previews ([9520798](https://github.com/matbo87/snes9x_3ds/commit/9520798f), [5a959fd](https://github.com/matbo87/snes9x_3ds/commit/5a959fd7))
* Added ROM Info dialog ([52fde3e](https://github.com/matbo87/snes9x_3ds/commit/52fde3e9))
* Added per-game crop/overscan control ([#55](https://github.com/matbo87/snes9x_3ds/issues/55)) ([c24f52c](https://github.com/matbo87/snes9x_3ds/commit/c24f52cb), [9cd70ac](https://github.com/matbo87/snes9x_3ds/commit/9cd70acb), [dd3ece5](https://github.com/matbo87/snes9x_3ds/commit/dd3ece5c))
* Added scanlines ([e739def](https://github.com/matbo87/snes9x_3ds/commit/e739def8))
* Added Mode 7 bilinear smoothing ([#68](https://github.com/matbo87/snes9x_3ds/pull/68))
* Added Frame Sync setting with VBlank/Sleep pacing options ([1692be4](https://github.com/matbo87/snes9x_3ds/commit/1692be44))
* Added audio buffer size setting ([6b5e022](https://github.com/matbo87/snes9x_3ds/commit/6b5e022b))

### Rendering & Compatibility
* Fixed HDMA/in-frame palette compatibility for games with mid-frame palette changes ([#73](https://github.com/matbo87/snes9x_3ds/pull/73))
* Added mosaic rendering support ([#70](https://github.com/matbo87/snes9x_3ds/pull/70))
* Fixed Mode 7 stale tile/texture issues ([e754a64](https://github.com/matbo87/snes9x_3ds/commit/e754a649), [b9080be](https://github.com/matbo87/snes9x_3ds/commit/b9080be1))
* Fixed stale core data after switching ROMs ([21ae864](https://github.com/matbo87/snes9x_3ds/commit/21ae8640), [d9e1932](https://github.com/matbo87/snes9x_3ds/commit/d9e19329))
* Fixed several game-specific rendering/timing issues ([93f6fd6](https://github.com/matbo87/snes9x_3ds/commit/93f6fd60), [56d46ee](https://github.com/matbo87/snes9x_3ds/commit/56d46ee4), [84fd3b7](https://github.com/matbo87/snes9x_3ds/commit/84fd3b76), [0045f54](https://github.com/matbo87/snes9x_3ds/commit/0045f54d), [5c49b44](https://github.com/matbo87/snes9x_3ds/commit/5c49b442))
* Optimized Mode 7 tile 0 and static palette updates ([#67](https://github.com/matbo87/snes9x_3ds/pull/67), [ac40c53](https://github.com/matbo87/snes9x_3ds/commit/ac40c538))

### Other Improvements
* Migrated audio output from CSND to NDSP and improved audio scheduling/stability ([#58](https://github.com/matbo87/snes9x_3ds/pull/58), [7ff81cc](https://github.com/matbo87/snes9x_3ds/commit/7ff81cc1), [f33eccf](https://github.com/matbo87/snes9x_3ds/commit/f33eccf4), [a300cf0](https://github.com/matbo87/snes9x_3ds/commit/a300cf05))
* Reduced `cpuexec.o` I-cache pressure and improved layout stability on Old 3DS ([#66](https://github.com/matbo87/snes9x_3ds/pull/66))
* Added detection and warning for savestates with a broken-audio signature ([10199ee](https://github.com/matbo87/snes9x_3ds/commit/10199ee8))
* Improved Citra compatibility for Mode 7 rendering and emulator detection ([861b714](https://github.com/matbo87/snes9x_3ds/commit/861b7147), [a17bea0](https://github.com/matbo87/snes9x_3ds/commit/a17bea09))


## v1.60.2

### Bug Fixes
* Fixed in-game freeze after toggling "Disable 3D" in menu ([#54](https://github.com/matbo87/snes9x_3ds/issues/54)) ([5251996](https://github.com/matbo87/snes9x_3ds/commit/52519966))
* Fixed SNES core regressions introduced by earlier cleanup commits ([14af419](https://github.com/matbo87/snes9x_3ds/commit/14af419), [fb200ab](https://github.com/matbo87/snes9x_3ds/commit/fb200abb))

### Other Improvements
* Reintroduced fast-forward hold hotkey and preserved legacy config compatibility ([#23](https://github.com/matbo87/snes9x_3ds/issues/23)) ([ce600fc](https://github.com/matbo87/snes9x_3ds/commit/ce600fc1))
* Minor UI adjustments ([e097bb6](https://github.com/matbo87/snes9x_3ds/commit/e097bb6d), [5b6188a](https://github.com/matbo87/snes9x_3ds/commit/5b6188a))


## v1.60.1

### Bug Fixes
* Fixed VRAM read control flow regression ([#46](https://github.com/matbo87/snes9x_3ds/issues/46)) ([c32c5ab](https://github.com/matbo87/snes9x_3ds/commit/c32c5ab))
* Fixed WindowLR overlap tagging when trimming black scanlines ([#46](https://github.com/matbo87/snes9x_3ds/issues/46)) ([d536983](https://github.com/matbo87/snes9x_3ds/commit/d536983))

### Reintroduced Features
* Reintroduced optional screen smoothing for stretched modes ([#51](https://github.com/matbo87/snes9x_3ds/issues/51)) ([432d202](https://github.com/matbo87/snes9x_3ds/commit/432d202))
* Reintroduced per-game framerate override (Auto or Force 60 FPS) ([#50](https://github.com/matbo87/snes9x_3ds/issues/50)) ([b4f45e8](https://github.com/matbo87/snes9x_3ds/commit/b4f45e8))

### Maintenance
* Document bundled makerom sources for provenance ([#47](https://github.com/matbo87/snes9x_3ds/issues/47)) ([4cae630](https://github.com/matbo87/snes9x_3ds/commit/4cae630))
* CI/tooling updates for GitHub Actions compatibility ([475042a](https://github.com/matbo87/snes9x_3ds/commit/475042a), [7e1a91a](https://github.com/matbo87/snes9x_3ds/commit/7e1a91a))


## v1.60

### Major Changes
* **Rendering backend migration**: move from legacy GPU code to citro3d
* **Draw-call batching overhaul**: fewer draw calls via batched rendering and XOR-based packed render-state diffing
* **GPU decoupling**: separate `gfxhw` state preparation from the GPU submission path for a cleaner rendering pipeline

### Performance
* **Rendering throughput**:
  * layer/section collection and merged backdrop/color-math passes to reduce redundant draws
  * uniform upload and render-state update optimizations (including patched citro3d max-dirty behavior)
* **I/O and memory**:
  * faster save/config writes and improved file I/O architecture
  * reduced heap fragmentation pressure
  * menu/file navigation streamlined, with snappier behavior on old 2DS/3DS models
  * improved ROM list caching
* **Asset handling**:
  * background assets on 16-bit texture formats (RGB565) to reduce memory bandwidth/footprint
  * replace `stb_image` with a `libpng`-based path that uses a shared file scratch buffer (`g_fileBuffer`) and an aligned shared stream buffer (`g_streamBuffer`) to reduce heap churn/fragmentation

### Features
* **Thumbnail system**:
  * replace fragile thumbnail background-thread loading with on-demand reads from one cache file per thumbnail type
  * removes shared-state race issues and keeps thumbnail loading fast and stable for large ROM folders
* **SNES-accurate refresh-rate matching**:
  * when gameplay starts/resumes, 3DS LCD timing is set to the game's native SNES rate (NTSC ~60.1 Hz / PAL 50 Hz)
* **On-Screen Display**:
  * bezel overlay with auto-fit support
  * FPS overlay option
  * GPU-accelerated notifications
* **Stereoscopic 3D additions**:
  * basic 3D support for splash screen, in-game scene background, and pause overlay

### Stability & Code Quality
* **Code-quality cleanup**: broad typing, const/sign correctness, return-path, warning cleanup across both 3DS frontend and SNES core
* **Build warning policy upgrade**: remove old global warning suppression (`-w`) and move to enabled warnings enforcing `-Werror` by default

### Breaking Changes
* **Config migration**: `settings.cfg` may not migrate cleanly in all cases; defaults can be applied
* **Thumbnail assets**: legacy per-image thumbnail folders are obsolete; thumbnails now load from `*.cache` files (`boxart.cache`, `gameplay.cache`, `title.cache`)
* **Background asset paths**:
  * `snes9x_3ds/borders` -> `snes9x_3ds/backgrounds/game_screen`
  * `snes9x_3ds/covers` -> `snes9x_3ds/backgrounds/second_screen`


## v1.52

### Bug Fixes
* **Thread safety**: prevent cache thread from accessing `romFileNames` while it is being modified by the main thread ([#32](https://github.com/matbo87/snes9x_3ds/issues/32)) ([ea806c5](https://github.com/matbo87/snes9x_3ds/commit/ea806c5d89d186f1d9018a8d1850190d422ad4ca))
* **Shutdown stability**: ensure all global/static stores are cleared properly at exit to avoid late destruction‑order crashes ([#32](https://github.com/matbo87/snes9x_3ds/issues/32)) ([ea806c5](https://github.com/matbo87/snes9x_3ds/commit/ea806c5d89d186f1d9018a8d1850190d422ad4ca))
* **ROM mapping**: Fix incorrect memory bank mapping for Mega Man X (and probably other ROMs) ([#26](https://github.com/matbo87/snes9x_3ds/issues/26)) ([05c6663](https://github.com/matbo87/snes9x_3ds/commit/05c6663ae2d944c4c232838ac4f5bf0d8c6c98aa))

### Features
* **Screen stretch**: add "8:7 Fit" scaling option ([#28](https://github.com/matbo87/snes9x_3ds/pull/28)) ([526d62f](https://github.com/matbo87/snes9x_3ds/commit/526d62f9250421ed867c43a675c226c63b718f19))
* **Screen filter**: add "linear filtering" option ([#28](https://github.com/matbo87/snes9x_3ds/pull/28)) ([9744318](https://github.com/matbo87/snes9x_3ds/commit/9744318cc747adeaf0c7decff94c8126584fa8b4))

### Code Refactoring
* **Performance**: revert commit 8d50f5 due to negative performance impact ([d50de94](https://github.com/matbo87/snes9x_3ds/commit/d50de943ff0415eb70f8cec3dfc7e50fe1490886))
* **Shader**: remove unused shaders + adjust makefile ([3aa0377](https://github.com/matbo87/snes9x_3ds/commit/3aa03772cecf17264e4cfee00545360286c15a42))


## v1.51.1

### Bug Fixes
* **Old 3DS, Old 2DS**: fix crash on O3DS/O2DS when user opens menu after game has loaded ([#11](https://github.com/matbo87/snes9x_3ds/issues/11)) ([71ed471](https://github.com/matbo87/snes9x_3ds/commit/71ed471f3f9bfea74f42105ddbbbf3b9e9a94c07))


## v1.51

### Features
* **Theme option:** add Dark mode and RetroArch theme ([#4](https://github.com/matbo87/snes9x_3ds/issues/2)) ([d343ca6](https://github.com/matbo87/snes9x_3ds/commit/d343ca60fb0e380fa9b4239c7ebf346e0ff86e6c))
* **File menu:** adjust navigation pattern + provide more options in file menu tab ([#4](https://github.com/matbo87/snes9x_3ds/issues/2)) ([d343ca6](https://github.com/matbo87/snes9x_3ds/commit/d343ca60fb0e380fa9b4239c7ebf346e0ff86e6c), [ea2cd3f](https://github.com/matbo87/snes9x_3ds/commit/ea2cd3fa970f81a4384ebf0c7b014b429d4d7d34))
  * Going up a directory by pressing B
  * Option to set a default starting folder
  * Delete game option
  * Random game option
* **Pause screen:** show a decent pause screen when menu is open during gameplay ([4c9f3ec](https://github.com/matbo87/snes9x_3ds/commit/4c9f3ecb333eaf23da85e9199bdbbfa3511312dd))

### Bug Fixes
* **O2DS**: fix crash on O2DS (and probably O3DS as well) when saving SRAM ([#2](https://github.com/matbo87/snes9x_3ds/issues/2)) ([02788b1](https://github.com/matbo87/snes9x_3ds/commit/02788b17d038e30e612dcbf0719ec45a8fc54a43))

### Code Refactoring
* **Menu**: reduce redundant code + preserve selected item index per tab ([493c1a2](https://github.com/matbo87/snes9x_3ds/commit/493c1a22b3975c7cb39a55dbd38140e5e3cd2a14), [4d6378a](https://github.com/matbo87/snes9x_3ds/commit/4d6378a507cb77571e4444abb6fbd0df3ff5f555))
* **Dialogs**: remove unnecessary animations for a snappier appearance ([2bb82c6](https://github.com/matbo87/snes9x_3ds/commit/2bb82c69512a2ef894ee5bb049be13ba567b6e89))
* **Second screen content**: clean up + prevent flickering when info dialog appears/disappears ([c1899df](https://github.com/matbo87/snes9x_3ds/commit/c1899df01828b9653c3c635695e61d1ce4fbeaee))


## v1.50

### Features
* **Game preview option:** boxart, title or gameplay
* **Improved cheat menu:** now with available/activated indicator
* **Updated Banner:** based on SNES VC banner
* **Menu clean up:**
  * Reset config(s) option
  * Autosave is now game-specific
  * Show saving dialog instead of _freezed_ menu

### Bug Fixes
* **Cheats**: Fix cheats not loaded/saved properly
* **Default button mappings**: Fix missing default controls 
* **Home menu button:** make emulator quit properly when user exits via home menu button
* **Pixel perfect mode:** Fix blurry image (mentioned [here](https://github.com/asdolo/snes9x_3ds_forwarder/pull/1))
* **Long game lists**: Fix app crash on exit

### Code Refactoring
* **Makefile & app.rsf:** use TricksterGuy's [3ds-template](https://github.com/TricksterGuy/3ds-template), update compiler options
* **Image loading/rendering:** use stb_image instead of lodepng for faster image decoding, unify image rendering logic
* **Second screen content**: improve performance

### Breaking changes
* **Folder structure:** All game related files are now in "3ds/snes9x_3ds", similar to RetroArch folder structure


---
## Older releases

### v1.45
- Buffered file writer for faster config saves (thanks to [willjow](https://github.com/willjow/snes9x_3ds))

### v1.42
- Fixed screen tearing
- Added option to disable 3D Slider (thanks to ramzinouri)

### v1.41
- Fixed hotkey for making screenshot 
- Fixed quick save/load (no data abort exceptions anymore)
- Fixed Errors if cover image is missing
- Updated assets (icon, banner, border, cover)

### v1.40

- Added Swap Game Screen option
- Added switch controller option like in official Virtual Console (SF2 "Training Mode", Konami cheat, ...)
- Custom second screen image and border for every game (thanks to ramzinouri and Asdolo)
- Game Info option for second screen
- Provide more Hotkeys (Quick Save/Load, Swap Controllers)
- Disable Analog to Digital Type option which allows you to use circle pad for hotkeys as well
- All game related files like cheats or save states are now in a single folder (folder name = rom name)
- Screenshots are now in PNG format (thanks to ramzinouri)
- Removed BlargSNES DSP Core, updated dsp-1, added dsp-2 -3 and -4 (thanks to ramzinouri)

### [bubble2k Change Log](https://github.com/bubble2k16/snes9x_3ds#change-history)
