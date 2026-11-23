---
id: BUG-1378
title: evo_screens_switch_lowfps: the guest's black-screen check counts the game's own stage-entry pause (BUG-0313) at 10 fps (12-22 s in 11 runs against the 20 s limit)
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: final check 20261120-133703
role: guest
versions: 
fixed_in: 0.7.16
branch: fix/w61-creature-b
commits: 14aa9dc9
tests: evo_screens_switch_lowfps, evo_screens_switch, evo_screens_switch_slow, evo_screens_switch_noready
signature: "FAIL the guest's screen is not black from the switch to the end of both editors (outside its own editor) (longest black 20 s)" with the guest's screen row "16s:Creature/black(0) 36s:Creature/drawn" at 10 fps
docs: docs/06-testing.md#testing-stage-entry-black
related: BUG-0313, BUG-1239, BUG-1240
---

## Report
Second final check of 0.7.16 (lab 20261120-133703, lane 3): evo_screens_switch_lowfps (the guest at 10 fps) FAIL "the
guest's screen is not black from the switch to the end of both editors (outside its own editor) (longest black 20 s)".
Every other check passed (both own editors, own creatures, no dialog).

## Root cause
* Observed (guest game log): the black run is 16-36 s after the guest's I'm ready: its game arrived in Creature at s 83.6
  (after the galaxy menu and the load of the host's world) and stood with `busy=paused` until ~s 104 (traces at 90 and 100
  s paused, at 110 not), no mod wait (no "this game waits for the host" until s 113, which is the host's caption later),
  no editor request, no cinematic. Key frame 02_host_editor_guest: black game, only the mod's overlay. That is the game's
  own stage-entry pause after a load (BUG-0313: the entry waits until every creature model is baked, its time paused, the
  screen black); BUG-1239's root cause lists it as "not changed" (the game's, longer at low fps).
* Measured: the same black row in all 11 runs of evo_screens_switch_lowfps since the test exists, 12-22 s (out/lab/
  20261118-100543 14, -131548 16, 20261119-120634 22, -124801 16, 20261118-110806 12, -134852 20, -144132 14,
  20261119-102828 18, -214823 12, 20261120-041304 18, 20261120-133703 20); at 60 fps 4-14 s. The test's limit (20 s,
  "what a player would call a black screen") sits inside that spread: a flake of the test that judged the game's own load
  as BUG-1239's black.

## Fix
Test only (coop_evo_screens.py Screens(entry=True) for the guest): each shot also reads coop.status (`busy:` has
`paused`, the mod's host-busy `wait=`). A black run that starts in a load mode (Loading / LoadGame / GalaxyGameEntry) or
right after one and goes on while the game's time stands by itself (not the mod's wait) is the game's own load and
stage-entry pause: it is shown as `entry` in the screen rows, measured on its own and must end within ENTRY_LIMIT (45 s,
2 x the longest seen), so a stage entry that hangs (or one the mod makes much longer) still fails. Every other black run
(the vote's hold, the partner's editor, an entry the mod holds with its wait, a black that is not paused such as
BUG-0920's flag) is still judged against the 20 s limit. Not loosened for the host (its screen behind the arrival vote is
the mod's hold).

## Verification
no-fail-run: a test change (the old test FAILED on the game's own load in out/lab/20261120-133703); the classification was
checked offline on the rows of that run (16-36 s become `entry`, longest black 6 s, entry 20 s). Lab, waiting for the lab
run: `SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_switch_lowfps evo_screens_switch evo_screens_switch_noready
--lane N --retry 0 --no-build` must PASS with the guest's rows showing `Creature/entry`.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-creature-b): the guest's own load and stage-entry pause measured apart (ENTRY_LIMIT 45 s).
- 2026-11-23: released in 0.7.16.
