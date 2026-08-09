# MSU-1 Wave 1 — Critical Hardware Test Plan

Prioritized by risk. Tests 1-4 attack the concurrency fixes that were made **without ever running on hardware** — the failures they guard against are **intermittent crashes**, so run each one several times; a single clean pass proves nothing.

Recommended test games: *The Legend of Zelda: A Link to the Past* MSU-1 hack (best coverage — uses the resume feature) or *Chrono Trigger* MSU-1 hack. Files (`Game.msu`, `Game-1.pcm`…) must sit next to the ROM with the **exact same basename**.

Run everything on **both** consoles where noted. Record results in the table at the bottom.

---

## Part A — Crash risk (concurrency fixes: run 10-30 repetitions each)

### Test 1 — Rapid track changes during playback
*(guards the file-close/file-read race on track switch)*

In Zelda: enter and leave houses/caves rapidly 20-30 times in a row — every transition switches the music track, which closes and reopens the PCM file while the audio thread is reading it.

- **FAIL:** intermittent freeze/crash, or corrupted audio on a transition.
- [ ] Old 3DS — result:
- [ ] New 3DS — result:

### Test 2 — Savestate load FROM THE MENU while music plays
*(guards the previously-unfenced menu load path)*

Save mid-song. Let it play a bit longer. Load via the **Savestates tab in the emulator menu** (this was the vulnerable path). Repeat ~10 times. Also test the quick-load hotkey separately (different code path).

- **FAIL:** crash on load, or music resuming from the wrong position.
- [ ] Menu load, Old 3DS — result:
- [ ] Menu load, New 3DS — result:
- [ ] Quick-load hotkey (either console) — result:

### Test 3 — ROM switch while music plays
*(guards the unload event inside the audio drain window)*

With music playing, return to the menu and load a **different game without MSU-1**. Then switch back to the MSU-1 game.

- **FAIL:** crash on switch, "ghost" audio of the old track playing over the other game, or MSU-1 not re-detected on return.
- [ ] Old 3DS — result:
- [ ] New 3DS — result:

### Test 4 — Console reset mid-song
*(guards the new reset event — this exact bug existed before the fix)*

Menu → Reset while music is playing.

- **FAIL:** the old track keeps playing over the rebooted game.
- [ ] Either console — result:

---

## Part B — State-mirroring behavior

### Test 5 — Long pause in the menu (position freeze)

Music playing → open the emulator menu → wait **30-60 seconds** → resume.

- **PASS:** music continues exactly where it stopped. **FAIL:** music "jumped ahead" (position advanced during the pause).
- [ ] Result:

### Test 6 — HOME menu and lid close (sleep)
*(hardware-only behavior — the sleep path differs between console models)*

With music playing: press HOME, wait, return. Then: close the lid, wait, open it.

- **FAIL:** music leaking into the HOME menu, position jump on return, or crash on wake.
- [ ] HOME, Old 3DS: — Lid, Old 3DS:
- [ ] HOME, New 3DS: — Lid, New 3DS:

### Test 7 — Resume feature (`$2007` bit 2 — newly implemented)

In Zelda: open the in-game item menu (overworld music pauses, jingle plays), then close it.

- **PASS:** overworld music continues from where it was. **FAIL:** it restarts from the beginning.
- [ ] Result:

---

## Part C — Robustness and performance

### Test 8 — Underrun count on Old 3DS (SD latency / syscore budget — never measured)

Enable the log file (Settings), play **10-15 minutes continuously** on the Old 3DS, open the menu, then read the MSU-1 underrun line in `sd:/3ds/snes9x_3ds/debug_v*.log`.

- **FAIL:** count keeps growing (audible as micro-gaps in the music). Report the number either way.
- [ ] Underrun count after ~15 min:

### Test 9 — Track looping

Let a looping track play to the end of the file and loop (2-3 loops).

- **FAIL:** click, silence gap, or jump at the loop point.
- [ ] Result:

### Test 10 — PCM files removed after saving a state

Save a state, **rename the `.pcm` files** on the SD card, load the state.

- **PASS:** game continues silently, no drama. **FAIL:** crash.
- [ ] Result:

### Test 11 — No regression in non-MSU games (zero-cost check)

Play ~5 minutes of a demanding regular game on the Old 3DS (something that already ran at the edge).

- **FAIL:** performance worse than before this branch.
- [ ] Result:

---

## Reporting

For any failure: note the test number, console, what you were doing at the exact moment, and whether it reproduces. Attach `sd:/3ds/snes9x_3ds/debug_v*.log` when relevant (especially Test 8's number, even if everything is fine).

Once Part A survives repetition on both consoles, the most dangerous part of the design is proven on hardware; Part B/C failures are fixable behavior bugs, not crashes.
