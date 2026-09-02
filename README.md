# Snes9x for 3DS

## Overview

This fork by [JorgeHSantana](https://github.com/JorgeHSantana/snes9x_3ds) adds **MSU-1 support** (CD-quality music and full-motion video streaming, with optional FLAC-compressed packs), **per-layer stereoscopic 3D** (every SNES layer — and each tile priority inside it — at its own depth, with a live in-menu editor and automatic per-screen Scene Profiles), **minutes of rewind** and a **built-in self-updater**, on top of [matbo87](https://github.com/matbo87/snes9x_3ds)'s modernized fork of the legacy snes9x_3ds codebase by [bubble2k](https://github.com/bubble2k16/snes9x_3ds).
It builds with current devkitARM, libctru and citro3d releases (as of June 2026). Optional assets are available in the dedicated asset repository: [snes9x_3ds-assets](https://github.com/matbo87/snes9x_3ds-assets).

It works on all 2DS and 3DS models.
Old 2DS/3DS mainly struggle with Super FX and SA-1 games, but most SNES titles run well.

Feedback and bug reports are welcome.

## Main features

* MSU-1 support: CD-quality music packs and FMV playback, with per-game MSU-1 volume and video FPS settings
* Compressed MSU-1 audio: `.flac` tracks play when the raw `.pcm` is absent (roughly half the SD space, still lossless)
* MSU-1 pack folders show up as single game entries in the file browser; ROMs also load from `.zip`
* Stereoscopic 3D with per-layer **and per-priority** depth: each BG's two tile priorities and each of the four sprite priorities get their own gauge, plus a focus zone with distance effects (fade, haze, depth-of-field blur), Edge Cleanup, and Enhanced Resolution rendering — all with zero extra draw calls (Old 3DS friendly)
* Live 3D editor in a dedicated **3D Stereo** tab: focusing a gauge spotlights exactly its tiles on the top screen, values move the paused frame in real time, and holding X previews the full scene
* Scene Profiles (experimental): capture a screen and its own 3D configuration is applied automatically whenever that screen shows up
* Self-updater: check and install new builds (Stable or Nightly channel) from inside the emulator, with optional check-on-boot
* File browser with instant cached listings that self-refresh in the background when SD contents change
* Improved rendering for HDMA-heavy games and mosaic effects
* SNES refresh rate matching (60.1 Hz for NTSC, 50 Hz for PAL)
* NDSP audio output
* Rich visual customization with thumbnails, themes, per-game backgrounds and overlays
* Crop and overscan
* Improved cheat management
* Rewind with **minutes** of history (page-delta storage: ~1.5min on New 3DS, ~1min on Old): hold the hotkey to rewind live, tap it for a browsable timeline of thumbnails
* 3DS Mode (New 3DS): drop to 268 MHz to preview how a game would run on an Old 3DS
* Extended hotkey options, controller port swap (P1/P2) and screen swap support
* Directory caching for faster ROM list loading

## Setup

* A modded 3DS is required; DSP firmware (`3ds/dspfirm.cdc`) is needed for sound output.
* Install via [Universal Updater](https://universal-team.net/projects/universal-updater.html), or install the latest `.cia` from [Releases](https://github.com/JorgeHSantana/snes9x_3ds/releases).
* After the first install the emulator keeps itself current: see [Updating the emulator](#updating-the-emulator).
* Optional: download asset packs from [snes9x_3ds-assets releases](https://github.com/matbo87/snes9x_3ds-assets/releases).

ROMs can be stored in any folder.

Supported ROM formats:
* `.smc`
* `.sfc`
* `.fig`
* `.bs`
* `.bsx`
* `.zip` (the first ROM inside is loaded; saves are keyed by the zip's name).
  This also works for the single ROM inside an MSU-1 pack folder — but the
  pack itself must stay a folder: audio/data tracks are streamed with random
  seeks, which zip compression cannot serve.

Configs, saves and imported assets are stored in `sd:/3ds/snes9x_3ds`.

### 3DSX version

* Copy `snes9x_3ds.3dsx` to `sd:/3ds/snes9x_3ds`
* Start it from the Homebrew Launcher

## Assets (images and cheats)

Assets are provided in a dedicated asset repository:
* [matbo87/snes9x_3ds-assets](https://github.com/matbo87/snes9x_3ds-assets)

Notes:

* The repository follows a 1G1R-style selection.
* Naming is strict No-Intro style for matching.

## Building from source

* Install devkitPro and 3DS toolchain packages (including devkitARM, libctru, citro3d). If needed, follow the [devkitPro pacman guide](https://devkitpro.org/wiki/devkitPro_pacman).
* The Makefile is based on TricksterGuy's [3ds-template](https://github.com/TricksterGuy/3ds-template).

Required command-line tools in `PATH`:

* For `3dsx` builds: `tex3ds`, `smdhtool`, `3dsxtool` (from the devkitPro 3DS toolchain).
* For `cia` builds: `makerom` in addition to the above.

Common build targets:

* `make 3dsx`
* `make citra`
* `make 3dslink` (sends the `.3dsx` to your Homebrew Launcher)

This repository bundles `makerom` binaries under `makerom/` for convenience.
Bundled binary provenance is documented in `makerom/BINARY_SOURCES.md`.

### Emulator status

* Citra (nightly ≤ 2104): working
* Azahar: Mode7 1024x1024 texture renders as a solid yellow texture

## Development and Contributions

New work lands on feature branches and is published to the `nightly` channel via GitHub Actions; tagged GitHub [releases](https://github.com/JorgeHSantana/snes9x_3ds/releases) are the stable line. Both channels are served to consoles by the in-emulator updater.

Community PRs are welcome. For larger changes, a short issue first is appreciated.
Please keep PRs focused and test on hardware where possible.
AI-assisted code is fine, but contributors are responsible for understanding and validating the code they submit.
Broad, risky, hard-to-review PRs may be closed or split into smaller changes. Prototype work may still be credited if it informs a later implementation.

AI note: I use AI assistants as part of my development workflow, including code review, debugging, planning, implementation and documentation. All changes are reviewed and adjusted by me before they are merged.

## Updating the emulator

The emulator updates itself — no PC or Universal Updater needed after the
first install. In the pause menu, **Emulator tab -> UPDATES**:

* **Channel**: **Stable** (recommended) gets the tagged releases; **Nightly**
  gets every development build. The auto-check never crosses channels — a
  Stable install is only ever offered Stable updates.
* **Check for Updates** looks for a newer build right away and shows what
  changed before asking to install. **Check on Startup** does the same
  automatically on boot.
* A **3DSX** install replaces its own file on the SD card; a **CIA** install
  goes through the system installer. Both take effect on the next launch.
  Downloads are verified before anything is touched — a failed or cancelled
  update (B cancels) never harms the running version.
* Updating **mid-game is fine**: the session is parked in a savestate on
  the SD card, the update runs with the memory to itself, and the next
  launch (the new build) resumes the game exactly where it was.

## Rewind: how to use

Bind the Rewind hotkey in the **Controls** tab (the Emulator menu also has an
"Open Timeline" action, no binding needed).

* **Hold** the hotkey: the game freezes, dims, and walks back through the
  stored moments while you hold — real-time speed at first, then
  accelerating, with a corner badge showing how far back you are. Release,
  and after a short countdown play resumes from that moment.
* **Tap** the hotkey: a timeline opens instead — a filmstrip of thumbnails
  with a dot strip and an elapsed label. **Y** previews the exact frame,
  **A** previews and asks to confirm (then the countdown runs), **B** goes
  back to where you came from.
* Settings live in **Emulator tab -> REWIND**: Recording on/off (disabling
  frees the history memory), Max History, Capture Patience and the Resume
  Countdown length.
* History: up to ~1.5 minutes on New 3DS (0.5s steps) and ~1 minute on Old
  3DS (2s steps). Loading a savestate resets the recorded history.

## Stereoscopic 3D: configuration tutorial

All of this lives in the pause menu's dedicated **3D Stereo** tab (it appears
between Settings and Controls when a game is loaded on a 3D-capable model —
New 3DS / New 2DS XL with the 3D slider open; on other models the emulator
falls back to plain 2D). Everything you configure is saved per game to
`sd:/3ds/snes9x_3ds/stereo3d/<game title>.3d` — named after the ROM's internal
title, so it matches the same game whatever the file is called — a small
text file you can share with other people.

### How depth works (-8 to +8)

Every depth plane has its own gauge: each background layer's two tile
priorities (**BG1-BG4, Prio 0 / Prio 1**) and the four sprite priorities
(**Sprites Prio 0-3**):

* **0** = the plane of the screen.
* **Positive** values pop the plane **out toward you** (+8 = strongest).
* **Negative** values sink it **into the screen** (-8 = deepest).

Why priorities? A single BG often carries two things at once — the floor at
priority 0 and detail drawn over the player at priority 1, say — and now
each can sit at its own depth. Sprites likewise: many games put the HUD, the
player and background props on different sprite priorities. **Start simple:
set a layer's two priorities to the same value** (that's the default — old
`.3d` files load that way too) and only split them when the live spotlight
shows you two groups of tiles that deserve different depths.

The 3D slider scales the whole thing, so configure with the slider fully up
and then use it to taste. Enhanced Resolution doubles the parallax
granularity, giving noticeably smoother depth steps — worth turning on if the
game runs well with it.

### The live editor

The gauges edit the actual paused frame, live on the top screen:

* **Focusing a gauge spotlights its tiles**: everything else dims to 30%,
  and only the tiles that gauge controls stay bright — so you can *see*
  what "BG2 Prio 1" actually is in this scene before deciding its depth.
* **Moving the value moves the layer in real time** (open the 3D slider
  while editing and watch the depth change as you press left/right).
* **Hold X** to peek at the full scene with your current values, no
  spotlight (Y keeps its usual page-up/down combo). The bottom bar shows
  the X "View" button while the tab is active.
* The **Enable / Disable Layers** toggles also apply live — useful to
  isolate a layer completely when the spotlight isn't enough.
* Leaving the gauges brings the normal paused look back.

**Slider Response** (in the tab's Settings section): **Discrete** (default)
snaps every shift to whole pixels so layers always move as one solid block;
**Continuous** keeps the analog slider feel but a layer can visibly split at
partial slider positions. At full slider both are identical.

### Configure the Default profile first

**The golden rule: the Default profile must look right on MOST screens of the
game** — normal gameplay above all. Scene Profiles exist for the few specific
screens that need something different (title screen, menus, a world map).
Doing it backwards — capturing profiles for everything and leaving Default
misconfigured — multiplies the work and invites wrong-profile matches; see
the warning in the next section.

### The layer-by-layer method

The reliable way to place each plane, using the spotlight:

1. Walk down the depth gauges one by one. The spotlight shows exactly which
   tiles each gauge owns — decide what that content is (far background?
   playfield? HUD?) and set the depth accordingly.
2. Hold **X** now and then to judge the whole scene together.
3. A gauge whose spotlight shows nothing simply has no tiles on this screen
   — leave it alone (or check another scene before deciding).

Guidelines that hold for most games:

* **Sprites at 0.** The player and enemies are what your eyes track; keeping
  them at the screen plane is the comfortable choice. Exceptions: screens
  where sprites are markers/cursors floating over a surface (world maps,
  menus) — there a positive pop looks great.
* The main playfield (usually BG1 or BG2) also close to 0.
* Distant scenery on negative values, HUD/text layers at 0 or slightly
  positive.
* Small differences read better than extremes: a scene spread across -4..+2
  usually looks deeper *and* more comfortable than one slammed to -8..+8.

### Focus zone and the depth effects

The **Focus Back / Focus Front** pair defines the "in focus" depth range
(defaults -1..+1). Layers inside it are drawn clean. The three **Effects**
gauges (0-8 each) are applied to layers **outside** the zone, growing
linearly with the distance beyond its edge:

* **Fade** darkens layers behind the zone (farther = darker).
* **Haze** washes them out toward the horizon (atmospheric perspective).
* **Blur** is a depth-of-field: it softens layers behind the zone **and**
  layers popping out in front of it. It is the most expensive effect
  (extra draw passes). **Blur Quality** in the tab's Settings defaults to
  **Auto**: full quality while the game holds its frame rate, a lighter
  single-ghost blur (half the cost) as soon as frames start dropping.

So "how strong is the effect on this layer" = gauge value x how far the
layer's depth sits from the focus zone. A layer at the zone's edge gets
nothing; the farthest layer gets the full gauge. The depth gauges gray out
in the menu when a layer sits outside the zone, as a visual cue.

### Edge Cleanup

The per-layer parallax disturbs the left and right screen borders (columns
that only one eye can see). **Trim** (default) crops those columns; **Zoom**
hides them by slightly enlarging the image; **Off** leaves them visible.
The setting lives in the tab's Settings section and applies to the whole
game — every layer, every Scene Profile.

### Scene Profiles: the exceptions

When one specific screen needs its own depths (title with a big logo, a
paused map, a menu), that's what Scene Profiles are for:

1. Get the screen on the display, then open the menu. Under **Scene
   Profiles**, use **Editing Profile -> + New Profile** (it copies the
   current selection and asks for a name right away).
2. Adjust the gauges — they always edit the selected profile.
3. Select **Capture This Screen** and return to the game: the emulator
   observes the screen for ~5 seconds and learns its fingerprint. From then
   on, that 3D setup applies automatically whenever the screen shows up,
   with a smooth transition.
4. Captured the wrong thing, or a screen keeps matching wrongly? Show that
   screen and use **Release This Screen** to send it back to Default.

The "This screen matches:" line at the bottom of the section always tells
you which profile the screen you paused on is using. Keep profiles few and
purposeful — Default should remain the workhorse.

## Experimental: Scene Profiles (per-screen 3D)

The Scene Profiles feature (3D Stereo tab) can bind
different 3D configurations to individual screens of a game (title, menus,
gameplay), switching automatically as scenes change. **This is experimental**:
scene detection relies on PPU register fingerprints and an optional WRAM
watch byte, and games reusing the same video setup across screens may match
the wrong profile. Multiple profiles can cause 3D instability in such cases -
if that happens, use "Release This Screen" or delete the profiles to return
to the single Default profile.

## Screenshots

<table>
  <tr>
    <td width="50%" align="center"><img src="screenshots/dark-mode-file-menu.png" alt="Start screen" valign="bottom"></td>
    <td width="50%" align="center"><img src="screenshots/retroarch-pause-screen.png" alt="Super Mario World" valign="bottom"></td>
  </tr>
  <tr>
    <td valign="top" width="50%" align="center">Start screen, "Game Thumbnail" option enabled</td>
    <td valign="top" width="50%" align="center">Pause screen, per-game overlay enabled</td>
  </tr>
  <tr><td colspan="2"></td></tr>
  <tr></tr>
  <tr>
    <td width="50%" align="center"><img src="screenshots/aladdin-pp-cheats.png" alt="Aladdin" valign="bottom"></td>
    <td width="50%" align="center"><img src="screenshots/dkc-hotkeys.png" alt="Donkey Kong Country" valign="bottom"></td>
  </tr>
  <tr>
    <td valign="top" width="50%" align="center">Cropped top & bottom, cheats enabled</td>
    <td valign="top" width="50%" align="center">Applied hotkeys</td>
  </tr>
  <tr><td colspan="2"></td></tr>
  <tr></tr>
  <tr>
    <td width="50%" align="center"><img src="screenshots/sf2-cropped-border-cover.png" alt="Super Street Fighter II" valign="bottom"></td>
    <td width="50%" align="center"><img src="screenshots/issd-screen-swap.png" alt="International Superstar Soccer Deluxe" valign="bottom"></td>
  </tr>
  <tr>
    <td valign="top" width="50%" align="center">Crop & overscan, scanlines enabled</td>
    <td valign="top" width="50%" align="center">Swapped screen</td>
 </tr>
 <tr>
    <td width="50%" align="center"><img src="screenshots/tg2-hdma.png" alt="Top Gear II" valign="bottom"></td>
    <td width="50%" align="center"><img src="screenshots/savestate-preview-bsx.png" alt="Excitebike - Bunbun Mario Battle" valign="bottom"></td>
  </tr>
  <tr>
    <td valign="top" width="50%" align="center">In-Frame Palette Changes enabled</td>
    <td valign="top" width="50%" align="center">BS-X game, savestate preview</td>
 </tr>
 </table>
 <br>

## Frequently Asked Questions

### A game runs slow. How can I improve performance?

* Increase `Frameskips` (more than 2 isn't recommended)
* Set `Frame Sync Method` to `Sleep Sync`
* Set `In-Frame Palette Changes` to `Disabled Style 1` or `Disabled Style 2`
* Set `SRAM Auto-Save Delay` to 60 seconds or disable it (SD Card speed is slow on 3DS)
* Disable 3D and/or on-screen display settings
* MSU-1 FMV: lowering `MSU-1 Video FPS` lightens rendering, but the
  emulation-side streaming cost is irreducible (see [docs/msu1.md](docs/msu1.md))

### A game looks or sounds wrong. What can I try?

* Set `In-Frame Palette Changes` to `Enabled`
* Increase `Audio Buffer Size` if audio crackles, skips or stutters
* Enabled cheats can break visuals or gameplay; disable cheats and reload the game
* Check if your ROM is valid (No-Intro is highly recommended; ROM hacks often have issues)
* Check the [known issues](KNOWN_ISSUES.md)

### Cheats are not working properly

* Cheat support is only lightly tested and some codes may not work correctly
* Use cheats with caution: broken codes can affect gameplay or damage save data

### Converting MSU-1 packs to FLAC

MSU-1 packs mandate raw PCM (~10MB per minute of music), so full packs reach
hundreds of MB. This port also plays `.flac` audio tracks: for each track it
first tries `<game>-N.pcm`, then `<game>-N.flac`. FLAC is lossless (identical
audio) at roughly half the size, and the loop point is preserved through a
`MSU1_LOOPPOINT` metadata tag read from the file.

Convert a pack on your PC with ffmpeg (bash):

```bash
for f in *.pcm; do
  loop=$(python3 -c "import struct,sys;print(struct.unpack('<I',open(sys.argv[1],'rb').read(8)[4:8])[0])" "$f")
  tail -c +9 "$f" | ffmpeg -f s16le -ar 44100 -ac 2 -i - \
    -metadata MSU1_LOOPPOINT=$loop "${f%.pcm}.flac"
done
```

Then delete the `.pcm` files from the SD card (keep the `.msu` data file —
video/data tracks must stay raw). Notes:

* Converted packs are specific to this port; SD2SNES and other emulators
  still need the raw `.pcm` files.
* Tracks must remain stereo 44.1kHz (the ffmpeg line above keeps them so).
* A `.flac` without the loop tag loops from the beginning of the track.

### Satellaview (BS-X) games

Satellaview games are supported, but compatibility is hit-or-miss.
See [Known Issues](KNOWN_ISSUES.md#satellaview-bs-x-games) for details and per-game status.

## License

Some files may carry their own license headers, but because this project includes the Snes9x core (`source/Snes9x/`), redistribution of the combined project follows the Snes9x non-commercial license terms.

See:
* [LICENSE.md](LICENSE.md)
* [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)

## Credits

* The Snes9x team for the SNES emulator core, and the libretro Snes9x core maintainers for ongoing reference work
* bubble2k, original author of [snes9x_3ds](https://github.com/bubble2k16/snes9x_3ds), for creating the excellent base this fork builds on
* matbo87, whose [modernized fork](https://github.com/matbo87/snes9x_3ds) (current toolchain, NDSP audio, UI overhaul) is the direct base of this one
* [JorgeHSantana](https://github.com/JorgeHSantana) — MSU-1 support, FLAC packs, per-layer/per-priority stereoscopic 3D with the live editor, Scene Profiles, rewind v2, the self-updater, zip loading and the other features of this fork
* [dr_flac](https://github.com/mackron/dr_libs) by David Reid (public domain) for FLAC decoding, and [doctest](https://github.com/doctest/doctest) for the host test suite
* Wyatt-James for his [snes9x_3ds fork](https://github.com/Wyatt-James/snes9x_3ds); this fork adapts safety, audio and stability fixes plus his SuperFX dispatch speedups
* rcmz's [parallax 3D fork](https://github.com/rcmz/snes9x-3ds-parallax-3d) — his per-priority depth slots and live layer preview shaped this fork's takes on both ideas (implemented differently here, shader-side, for Old 3DS performance); his real Mode 7 perspective remains on our wishlist
* ramzinouri's [snes9x_3ds fork](https://github.com/ramzinouri/snes9x_3ds) inspired the image border/background and theme support
* willjow's [snes9x_3ds fork](https://github.com/willjow/snes9x_3ds) revived the project after development had gone quiet
* Tyler Sanders for the first stereoscopic 3D attempt (snes9x_3ds_3D); the stereo 3D work in this fork started from his phase-1 shader groundwork
* The Citra/Azahar teams for making 3DS emulator testing and debugging practical
* Everyone reporting issues, testing games and suggesting improvements
