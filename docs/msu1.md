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

MSU-1 doesn't have its own menu section anymore — it's part of the pause menu's **Settings** tab, unified **Audio** section, alongside the SNES volume:

* **SNES Volume** — the existing per-game/global volume amplification picker (unchanged).
* **Apply volume to all games** — the existing global-vs-per-game switch. It now governs BOTH volumes: when on, the global SNES volume and the global MSU-1 volume apply to every game; when off, each game uses its own saved values.
* **Audio Buffer** — unchanged.
* **MSU-1 Volume** (gauge 0-8) — shown only for the currently loaded MSU-1 game. Balances MSU-1 track volume against the game's own audio; 4 is neutral (1.0x), 0 mutes MSU-1, 8 doubles it. Applies live. Whether it reads/writes the global or the per-game value follows the "Apply volume to all games" switch above, exactly like SNES Volume.
* MSU-1 activates automatically when the `.msu` file is present. There is no enable/disable option by design.
* **Status line + subtitle** — a snapshot taken each time you open the menu, shown at the bottom of the Audio section:
  * `MSU-1: disabled` — the per-game "Enable MSU-1" setting is off; the chip is torn down for this game.
  * `MSU-1: not detected` — this game has no `.msu` file (or it wasn't found), and MSU-1 is enabled.
  * `MSU-1: detected` — the chip is active but not currently playing a track.
  * `MSU-1: playing track N` — a track is streaming right now.
  * `Minor audio stutter detected` (subtitle) — a few audio underruns happened this session; usually not noticeable.
  * `Audio is stuttering - a faster SD card may help` (subtitle) — underruns are frequent enough to be audible, most likely on Old 3DS with a slow SD card.

> **Disabling MSU-1 can mean silence, not a fallback.** Some hacks (mostly audio-only ones) removed their original SPC music entirely and rely on MSU-1 for all music. Turning "Enable MSU-1" off for those games doesn't restore the original soundtrack — it just goes quiet where music would play. Only disable MSU-1 on a hack you know still has working non-MSU audio.

## MSU-1 Video FPS cap: what it saves — and what it cannot

Lowering **MSU-1 Video FPS** gives a smaller gain than the setting suggests.
Paced-out frames are genuinely cheap on the render side: the Bresenham pacer
(`msu1_pace_step`, source/3dsimpl.cpp) sets `IPPU.RenderThisFrame = FALSE`
before the frame runs, so the PPU render pass early-outs (gfx.cpp,
ppuvsect.cpp) and the GPU side is skipped entirely — no layer passes, no
composite.

What can NOT be skipped: `S9xMainLoop()` runs in full on every frame. For
MSU-1 FMV that is where most of the cost lives — the "video" is not a file
the emulator decodes; it is the game itself streaming tiles from the data
track into VRAM via DMA, inside the emulation. Skipping that would corrupt
VRAM for the next presented frame and desync the game. Tiles uploaded during
skipped frames also still get converted to 3DS textures on the next drawn
frame — deferred, not eliminated.

So the cap trims the render/GPU share of the frame cost; the emulation +
streaming + amortized tile-conversion share is irreducible by design.
Pairing the cap with regular frameskip does not change this (frameskip works
the same way). If FMV performance ever needs another push, the lever is a
cheaper emulation-side FMV path (e.g. cheaper VRAM-write/tile-cache
invalidation during bursts), not more aggressive frame dropping.

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
