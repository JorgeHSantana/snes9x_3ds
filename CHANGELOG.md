# Changelog
Notable changes to this project will be documented in this file.

## Unreleased (nightly)

### Features
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
