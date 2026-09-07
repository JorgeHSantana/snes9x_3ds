# Azahar validation harness (issue #69)

Turns the hand-made "boot a scene, screenshot, diff" ritual into one
command, so a 3D change ships with a pixel proof instead of "looks right".
It does not replace the console: see `docs/testing/hardware-checklist.md`
for what only hardware can validate.

## What it does

1. Arms a **scene** in Azahar's SD image: `autoboot.txt` (ROM), the
   savestate as `savestates/<rom>.update.frz` plus `update-resume.txt`
   (the updater's resume marker - the emulator loads ROM + state on boot
   with **zero key presses**), the game's `stereo3d/<title>.3d`, and
   `settings.cfg` overrides (existing keys only - see the caveat below).
2. Launches the `.3dsx`, waits, pins the Azahar window to a canonical
   geometry and captures the top screen (400x236).
3. Diffs the capture against a golden PNG, or against a second scene
   (A/B), per named region, and writes a `.diff.pgm` mask.
4. Restores every SD file it touched and kills Azahar (`kill -9`: a clean
   exit would rewrite the `.3d` and `settings.cfg` under test).

```
tools/azahar-validate/validate.py list
tools/azahar-validate/validate.py ab  mmx3-pillar-blur6 mmx3-pillar-blur0
tools/azahar-validate/validate.py run mmx3-pillar-blur6 --update-golden
tools/azahar-validate/validate.py run mmx3-pillar-blur6
```

Exit code 1 when a region falls outside its `expect` / `ab_expect` band.

## Build flags the scenes need

| flag | effect | needed by |
|---|---|---|
| `-DPROBE_FORCE_SLIDER` | `osGet3DSliderState()` reads 1.0 (Azahar never delivers a slider value) | every 3D scene |
| `-DPROBE_DRAW_STATS` | `[drawstats]` per-frame draw/vertex counts in the session log every 60 frames | perf numbers |
| `-DPROBE_SBS` | both eyes composite side by side on the top screen (left half = left eye) so one capture carries the per-row disparity (`"sbs": true` scenes) | stereo geometry (Mode 7 perspective) |

```
make clean && make 3dsx EXTRA_DEFINES='-DPROBE_FORCE_SLIDER'
```

Objects do not track defines: **`make clean` on every flag flip, in both
directions** - a probe build shipped by accident is a real risk.

## Scene file

```json
{
  "title": "what this proves",
  "rom": "sdmc:/snes/vanilla/Mega Man X3.smc",
  "state": "states/mmx3-pillar.frz",
  "stereo3d": [{ "file": "MEGAMAN X3", "content": "BG1=0\nBG1P1=4\n..." }],
  "settings": { "StereoBlurQuality": "1" },
  "wait": 32,
  "regions":   { "city_bg_P0": [x0, y0, x1, y1] },
  "expect":    { "city_bg_P0": [0, 0.5] },
  "ab_expect": { "city_bg_P0": [0, 0.5], "pillar_P1": [2, 100] },
  "log_expect": ["ROM loaded: MEGAMAN X3"],
  "log_forbid": ["[blur] auto -> light"],
  "frames": 6,
  "temporal_expect": { "sky_forest_BG2": [0, 0.5] }
}
```

* `rom` is the path **inside the SD image**; ROMs are not in this repo.
* `state` is repo-relative (`states/`); `.3d` files are keyed by the ROM's
  header title since v2.1.
* `regions` are top-screen pixel boxes; bands are percentages of changed
  pixels (sum of channel deltas > 40).
* `wait_log` gates the capture on a session-log substring instead of a fixed delay (e.g. `"2105=07"`, the scene matcher's PPU mode-7 signature), then waits `settle` seconds.
* `sbs: true` (with a `-DPROBE_SBS` build) prints the per-row-band disparity between the two eyes; `sbs_expect.min_growth` asserts how much it changes between the top rows and the bottom rows (`horizon_deeper: true` asserts the top rows carry that much MORE shift than the bottom ones - the Mode 7 anchor since the horizon recedes past the gauge).
* `frames` > 1 captures that many frames 0.3 s apart and reports the worst
  consecutive-frame diff per region (`temporal_expect`): a static region
  that changes between frames is flicker or wobble.

## Caveats that cost hours before

* **`settings.cfg` is parsed sequentially per version block.** Inserting
  a key by hand derails every key after it (a hotkey once vanished and
  was saved back as 0). The harness only edits keys that already exist;
  boot the build once so it writes the current version first.
* **Key presses are unreliable** in Azahar 2126 (roughly one in three is
  lost; 0.2 s is missed, 0.5 s auto-repeats). The harness avoids keys
  entirely; if a scene needs them, capture after every press and verify.
* **The display must be awake and unlocked** - `screencapture` returns
  "could not create image" otherwise. The harness runs `caffeinate -d`.
* The session log is named by version (`debug_v2.1_session.log`).
* Azahar's Vulkan backend logs "Unimplemented reinterpretation RGBA8 ->
  D24S8" for our depth texture used as a color target - Azahar's
  limitation, the pattern is validated on hardware.

## Adding a scene

Make a savestate on the console or in Azahar (slot 1 = `<rom>.1.frz`),
copy it into `states/`, write the scene JSON, run with `--update-golden`
once you have checked the capture by eye, and commit both.
