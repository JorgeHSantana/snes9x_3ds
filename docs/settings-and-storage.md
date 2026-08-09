# Settings, Configuration and SD Card Layout

[← Back to index](README.md)

## Settings model (`3dssettings.h` / `.cpp`)

* Single global object: `S9xSettings3DS settings3DS`.
* Enums under namespace `Setting`: `ScreenFilter{Sharp,Smooth,Balanced}`, `ScreenStretch{None (256px), 4:3 (298px), CRT (292px, 8:7 PAR), Fit 4:3 (320×240), Fit 8:7 (274×240), Full}`, `ThumbnailMode`, `AssetMode{None,Default,Adaptive,CustomOnly}`, `Theme`, `Font`, `Framerate{UseRomRegion,ForceFps60}`, `FrameSync{VBlank,Sleep}`, `Intensity3D`, `EnhancedResolution{Off,Standard,Wide}`.
* **Global vs per-game**: controls (button mappings ×4 slots, 9 hotkeys, 8 turbo settings, circle-pad binding) exist in both flavors with `UseGlobal*` switches. Video/performance options are per-game: crop/overscan, max frame skips (0-4), framerate, frame sync, palette fix (+ BG defer mask), Mode 7 bilinear, enhanced resolution, volume, audio buffer, auto-savestate, save-state screenshots, SRAM save interval, force-SRAM-write-on-pause.
* `settings3dsUpdate()` recomputes derived values: `TicksPerFrame` (region/override), volume, `PaletteFix` → `SNESGameFixes.PaletteCommitLine` (1→−2 deferred, 2→line 1, 3→−1), `SRAMSaveInterval` → `Settings.AutoSaveDelay` (60/600/3600/off frames), and copies global→per-game control arrays where `UseGlobal*` is set.
* Per-title heuristic for the palette-fix default on Old 3DS (`settings3dsGetGameDefaultPaletteFix()`): New 3DS always "Enabled"; a handful of titles get specific modes (Bahamut Lagoon, Secret of Mana, Kirby Super Deluxe…).
* Runtime-only fields (never persisted): `RootDir`, screen dims, `TicksPerFrame`, `TurboMode`, `LayerEnabled[8]`, dirty flags, `isReal3DS`-adjacent state.

## Config file format (`3dsconfig.cpp`)

* INI-ish `key=value` text; **versioned**: global file target v1.8, per-game v1.6. The file starts with `# v<x.y>`; readers skip keys newer than the file's version (no spurious parse warnings), writers always write everything.
* Primitives: int (with min/max clamping; comment lines supported), string (empty-value safe), bitmask, and a templated enum round-trip.
* Writes go through `BufferedFileWriter` (buffering into the shared 512 KB `g_fileBuffer`); reads through buffered stdio.

## SD card layout (`sd:/3ds/snes9x_3ds/`)

Directories are created on first run by `file3dsInitialize()`.

| Path | Contents |
|---|---|
| `settings.cfg` | global config |
| `configs/<rom>.cfg` | per-game config |
| `saves/<rom>.srm` | SRAM |
| `savestates/<rom>.{1..5}.frz`, `.auto.frz` | save states (5 slots + autosave) |
| `savestates/<rom>.N.frz.broken-audio.log` | broken-audio diagnostics |
| `savestates/screenshots/<rom>/N.png` | save-state preview screenshots |
| `screenshots/<rom>.<timestamp>.png` | manual screenshots |
| `cheats/<trimmed>.chx` / `.cht` | cheats (text / legacy binary format) |
| `overlays/<trimmed>.png` or `_default.png` | bezel overlays |
| `backgrounds/game_screen/…`, `backgrounds/second_screen/…` | per-game or default background images |
| `thumbnails/{boxart,title,gameplay}.cache` | thumbnail packs |
| `.dir_cache/<sanitized path>` | directory listing caches |
| `debug_v<ver>_session.log` | session log when enabled |

Plus, embedded in the app's romfs: `romfs:/mappings.txt` (name aliases) and `romfs:/gfx/*` (default overlay, backgrounds, splash atlas).

Satellaview BIOS: `3ds/snes9x/BS-X.bin` (exact casing) or `BS-X.bios`.

## Asset name matching

Assets (thumbnails, cheats, per-game images) are matched **by ROM name**, not checksum:

```
ROM basename
  → strip region/dump tags "(...)" and "[...]"   (utils3dsGetTrimmedBasename)
  → apply romfs:/mappings.txt aliases            (e.g. "Kirby's Fun Pak" → "Kirby Super Star")
  → canonical asset name
```

This is why the companion asset repo uses strict No-Intro naming with a 1G1R selection.

## Thumbnail cache format

Two binary formats read by `img3dsSetThumbMode()`:

* `"IMGZ"` — uniform dimensions, 8-byte index entries.
* `"IMG2"` — per-entry dimensions, 12-byte entries (added post-v1.61).

Header `{magic[4], count, width, height}`; entries `{gameID (DJB2 hash of trimmed basename), offset, width, height}`. Max 1024 entries, max 128×128 px. Pixel data is stored **pre-swizzled column-major**, so drawing to the second-screen framebuffer is a straight per-column `memcpy`.
