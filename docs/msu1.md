# MSU-1 Support

[← Back to index](README.md)

## What is MSU-1?

MSU-1 is a fan-defined SNES enhancement chip used by ROM hacks and translations (e.g. *Zelda: A Link to the Past MSU-1 Edition*, *Chrono Trigger MSU-1 Edition*) to stream CD-quality audio and full-motion video from the SD card instead of the SNES's native 32 KB APU sample RAM.

## Status (wave 1)

* **Supported now**: MSU-1 streamed audio (music/BGM tracks), track switching, looping, resume-from-savestate.
* **Not yet supported**: MSU-1 video/FMV data reads. This is planned for a separate wave 2 plan (read-ahead tuning, per-game disable setting) written after wave-1 hardware validation results are in.
* Games that don't use MSU-1 are unaffected — detection is a single file-existence check at ROM load, and everything else is a no-op when no `.msu` file is present.

## Usage

MSU-1 games need their data files placed **next to the ROM**, using the **exact same base filename** as the ROM (case-sensitive on the SD card), with the ROM's own extension stripped:

```
sd:/3ds/snes9x_3ds/roms/Zelda MSU-1 Edition.sfc
sd:/3ds/snes9x_3ds/roms/Zelda MSU-1 Edition.msu
sd:/3ds/snes9x_3ds/roms/Zelda MSU-1 Edition-1.pcm
sd:/3ds/snes9x_3ds/roms/Zelda MSU-1 Edition-2.pcm
sd:/3ds/snes9x_3ds/roms/Zelda MSU-1 Edition-3.pcm
...
```

* `<romname>.msu` — the MSU-1 data/track-index file (required; its presence is what triggers MSU-1 mode for the ROM).
* `<romname>-N.pcm` — one PCM audio file per track number `N` referenced by the ROM (1, 2, 3, …). Not every game uses sequential numbering — only place the tracks the game actually calls for.
* **No ZIP support.** The ROM itself must not be zipped, and the `.msu`/`.pcm` files must be plain files on the SD card next to it — snes9x_3ds does not look inside archives for MSU-1 assets.
* If the `.msu` file is missing, the game runs exactly as a normal ROM (silent APU-only audio) — nothing crashes or errors out.
* If individual `.pcm` track files are missing while `.msu` is present, that track plays silent when selected; the game continues running normally.

## Settings

The pause menu's **Settings** tab has an **MSU-1** section (next to Audio) with:

* **Status line** — a snapshot taken each time you open the menu:
  * `MSU-1: not detected` — this game has no `.msu` file (or it wasn't found).
  * `MSU-1: detected` — the chip is active but not currently playing a track.
  * `MSU-1: playing track N` — a track is streaming right now.
  * `Minor audio stutter detected` (subtitle) — a few audio underruns happened this session; usually not noticeable.
  * `Audio is stuttering - a faster SD card may help` (subtitle) — underruns are frequent enough to be audible, most likely on Old 3DS with a slow SD card.
* **Enable MSU-1** (checkbox, per-game) — turns MSU-1 playback on/off for the current game, applied immediately without reloading. When off, the game falls back to its normal SNES audio.
* **MSU-1 Volume** (gauge 0-8, per-game) — balances MSU-1 track volume against the game's own audio; 4 is neutral (1.0x), 0 mutes MSU-1, 8 doubles it. Applies immediately.
* **MSU-1 Default Volume** (gauge 0-8, global) — the starting "MSU-1 Volume" for games that don't have a saved per-game config yet.

## Known limitations (wave 1)

* **DMA transfer mode 0 only.** DMA from the MSU-1 data port honors transfer mode 0 (single B-bus address) — the pattern audio hacks use. Multi-address transfer modes 1-4 (the patterns FMV hacks use to blast data into VRAM/WRAM) are not yet wired to the MSU-1 source; they come in wave 2 together with the data-throughput work.
* Fast-forward keeps consuming PCM while muted, so the track position drifts during turbo — inherent to fast-forwarding streamed audio.

> **Prioritized, detailed test plan with repetition guidance and result tracking: [msu1-hardware-tests.md](msu1-hardware-tests.md)** — start there; the list below is the short-form checklist.

## Hardware validation checklist (run on BOTH Old 3DS and New 3DS)
Game: Zelda ALttP MSU (or Chrono Trigger MSU)
- [ ] Boot: title music plays (CD quality, no stutter)
- [ ] Track change on area transition
- [ ] Pause menu: music mutes; resume: music continues from same position
- [ ] Fast-forward: MSU music mutes, resumes after
- [ ] Savestate save + load: music resumes from saved position
- [ ] Savestate load with .pcm files removed: no crash, game continues silent
- [ ] HOME menu + return: music resumes
- [ ] Sleep (close lid) + wake: music resumes
- [ ] Switch to a non-MSU ROM: no leftover audio
- [ ] Switch back: MSU re-detected
- [ ] Volume setting change applies to MSU audio
- [ ] Non-MSU games: no behavior change at all
- [ ] Old 3DS: check underrun count in log after 10 min of play
