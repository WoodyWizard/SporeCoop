---
id: BUG-1410
title: nest_target_probe: the host draws the guest, held in the air in its paused game 565 m away, 6.3 m lower than the guest's game has it (monitor drawn)
status: open
kind: desync
stage: Creature
reported: 2026-11-21
source: lab rel/0.7.16 20261121-003202 nest_target_probe
role: 
versions: 0.7.13, unreleased
fixed_in:
branch: fix/w62-flake-cc
commits: 91754ef4
tests: nest_target_probe
signature:
docs:
related: BUG-0425, BUG-1376
---

## Report
0.7.16 third final check (lab 20261121-003202, lane 3): nest_target_probe RESULT OK, failed by the lab's monitor gate
"drawn: host draws guest 6.3 m from where guest's own game has it (limit 3.0 m, Creature) on 2 lines from s 120". The
same in rel/0.7.13 20261112-175447 (6.8 m from s 130) and fix/w48-creature 20261112-075549 (6.3 m at s 120).

## Root cause
Observed (host and guest trace lines aligned by the 10 s marks, all runs of the test):
* the test puts the guest's creature (`coop.npc gopos`) 300 and 600 m from the host while the guest's game is paused
  (its line: `busy=cinematic,input_lock,paused`): it hangs there in the air; at the two 600 m spots it is 21.82 m and
  36.10 m over its ground (the same numbers in every run: the spots are fixed).
* failing runs: the host's puppet of it is drawn 15.48 / 29.76 m over the host's ground (20261121-003202), 15.06 /
  29.34 (20261112-175447), 29.49 vs 35.78 (20261112-075549): 6.3-6.8 m lower, the distance the monitor measures.
  Passing runs at the same spots (20261120-133703): drawn 21.82 / 36.10, exactly the guest's.
* the host's trace agl and drive::at_sender_agl both read GetHeightAt; the drawn absolute radius is the guest's minus
  6.3 m, so at the moment the drive placed the puppet the host's (or in the sent state the guest's) ground under that
  spot read ~6.3 m different from what it reads at the trace mark.
Inferred: the terrain under a spot 565 m from the host's camera (or the guest's, just moved there, paused) is not at
its final detail when the height is read; which game's value is off is not in the logs. Not the w61 change: BUG-1376's
drive.lead_gap acts only after a gap of over 2 s between two states, and the states here come every frame (state_age 0.0);
the same offset was seen in 0.7.13. A player hardly meets it (a partner hanging still in the air far away), but a
gliding or falling partner far from the host may be drawn metres off for the same reason.

## Fix
None yet (cause not proven). The test prints both games' `coop.ground` (GetHeightAt radius, the snap surface, the
point's height over them) under the guest at every spot, so the next run that shows the offset names the game whose
terrain disagrees. The monitor rule is unchanged.

## Verification
Lab (shows the cause): `SPORE_DETECTOR=20 python3 tools/lab.py run nest_target_probe --repeat 4 --lane N --retry 0
--no-build`: in a run with the `drawn` violation, the line "ground under the guest: host ..., guest ..." at the 600 m
spots. Open: waiting for that run, then the fix.

## History
- 2026-11-21: opened from the third final check of 0.7.16 (fix/w62-flake-cc); diagnostics added to the test.
- 2026-11-23: again in lab 20261122-194210 (nest_target_probe, lane 7, gate monitor drawn 6.3 m, the test RESULT OK): the same numbers (agl 15.48 / 21.82 at s 120, 29.76 / 36.10 at s 130, d=565), and the new ground lines show both games read the same ground there (dir 1 483.55 / 483.55, dir 3 469.27 / 469.27): the 6.3 m is in the host's drawn placement, not in GetHeightAt. Still open, 0.7.16.1.
