# Hardware checklist (issue #69)

What only the console can validate. Ten focused minutes per candidate
build; everything else goes through `tools/azahar-validate/`.

## Before a nightly promotion / stable cut

1. **Real stereo** (New 3DS, slider up): one split-priority scene (MMX3
   stage-1 pillar) and one Mode 7 game. Look for: layers moving as one
   block (no half-pixel tearing), no hole between a BG's priorities, blur
   ghosts only on the out-of-focus priority, edge cleanup with no colored
   columns.
2. **Performance under effects**: DBZ Super Butoden 2/3 with 3D + blur.
   Watch the Blur Auto log line (`[blur] auto -> light|full`) and the
   frame pacing; Light should engage on the split screen and Full return
   in a plain fight.
3. **Old 3DS / 3DS Mode**: SMW and one Super FX or SA-1 title at 268 MHz.
   Any new stutter is a regression (issue #59 class).
4. **Updater over Wi-Fi**: Check for Updates with a game loaded, apply,
   choose "keep playing" (the game must reload where it was), then exit
   and relaunch (the new build must start). Look at
   `[upd] heap before net` in the log.
5. **Auto Save / Auto Load**: with the option on, pause (menu), press
   HOME, close the lid, power off from HOME; relaunch and confirm the
   game resumes. `[autosave] <reason>: saved` in the log for each.
6. **SD migration**: after the first boot of a build that changes file
   keys (e.g. `stereo3d/<title>.3d`), the old files still exist and the
   new ones appear (`[stereo3d] migrated` in the log).
7. **Sleep/wake**: close the lid mid-game, wake - audio back, no dark
   screen.

## Always

* Session log enabled (Settings > Log) on the test console, and attach
  the relevant lines to the issue.
* Note the model (New/Old), the CIA vs 3DSX install, and the exact
  release name from the updater.
