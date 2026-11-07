---
id: BUG-0663
title: long_tribe: both games of a lane drop to 2-9 fps in Tribe under a full lab load (shared headless Xwayland saturated), not a 0.7.9 regression
status: open
kind: test
stage: tribe
reported: 2026-11-06
source: final check 0.7.9
role: 
versions: 0.7.9
fixed_in:
branch:
commits:
tests: long_tribe, long_tribe_short, tribe_play
signature: perf: fps avg 2-9, 130-200 hitches of 300-600 ms per minute in Tribe on both games of one lane, games at 5-7 % CPU
docs: docs/06-testing.md#testing-xwayland-saturation
related:
---

## Report
0.7.9 final check (rel/0.7.9 f44b2c87, lab 20261106-231404, lane 7, 4 lanes in parallel): long_tribe FAILED at
~s 1050, "host: the console stopped answering (crash?)", no crash, 0 faults. Guest fps 36 (s 766) -> 22 -> 8 (s 887,
before the editor) -> 2-4; host 50 -> 4 in the Tribe editor -> 9 back in Tribe, a 330-350 ms hitch every frame. The
mod's tick ~0.01-0.3 ms/frame, address space fine (3350+ MB free). long_tribe passed on 0.7.8 (lab
rel/0.7.8 20261105-155518, a 1-lane run).

## Root cause
Not a 0.7.9 regression: the lab machine's shared headless display.

* Observed, A/B at the same time (lab 20261106-235016, `long_tribe_short`, both while the 4-lane final check ran on
  lanes 6, 7, 9, 10): 0.7.9 (fix/w46-tribe-fps = rel/0.7.9 f44b2c87, lane 4) and v0.7.8 (rel/0.7.8 18f8d657, lane 5)
  collapsed the same way at the same moment: both guests 38-44 fps -> 7 / 4 fps at s ~273, then 3 fps (158 / 157
  hitches per minute); both hosts 3 fps at s ~336. Both FAILED (0.7.9: guest FROZE; 0.7.8: host console stopped
  answering, the final check's symptom). Before that (tribe_play, lab 20261106-234818, 100 s) both passed with the
  same start numbers.
* Observed: during the collapse the games used 5-7 % CPU each (they wait, they do not compute) and the wineservers 2-4 %;
  the one Xwayland of the headless shell (`:2`, every lane's DISPLAY, running since 2026-10-24, 342 000 CPU-s) used
  97.7 % of a core (one thread: saturated). Load average 12-14 on 12 threads, swap 8 of 8 GB used, two browser
  content processes at 99 %.
* The 0.7.8 PASS was a 1-lane run (2 games); the 0.7.9 final check ran 4 lanes with 4-game campaign tests (10-14 games
  on the one Xwayland). Cell and Creature tests of the same run kept 25-66 fps, Tribe tests (campaign_tribe lane 9,
  campaign_ct, tribe_civ_ground) had 80-165 hitches per minute on 0.7.9 as on 0.7.8 here: Tribe draws the most.
* Inferred: Wine presents every frame through that Xwayland; when it saturates, the games of the heaviest stage starve
  and a 40-min test's console times out.
* Earlier 0.7.9 lab runs: 20261106-153514 (b99109cb, 1 lane) Tribe 0-5 hitches per minute, 20261106-200316 (4ed96664,
  1 lane) 112-176: the same code paths (the lead, member body) did not cause the A/B difference above, so that run was
  inferred to be load too (other agents' lanes at 20:03), not proven.

## Fix
None in the mod. Lab: run long_tribe on a quiet machine (1-2 lanes, or after the final check), or restart the headless
shell (tools/headless.sh, owner's call: it ends every lane's games) before a 40-min test; a lab gate on the Xwayland's
CPU (over ~90 % of a core = the run's fps numbers are not the mod's) would mark such failures as load.

## Verification
no-fail-run: not a mod bug; A/B lab 20261106-235016 in fix/w46-tribe-fps (0.7.9, FAIL) and rel/0.7.8 (0.7.8, FAIL),
same collapse at the same time; tribe_play 20261106-234818 in both trees passed.

## History
- 2026-11-06: opened (fix/w46-tribe-fps): A/B 0.7.9 vs 0.7.8 at equal load, both collapse; shared Xwayland at 98 %.
