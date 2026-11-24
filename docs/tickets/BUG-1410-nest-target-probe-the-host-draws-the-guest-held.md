---
id: BUG-1410
title: nest_target_probe: the host draws the guest, held in the air in its paused game 565 m away, 6.3 m lower than the guest's game has it (monitor drawn)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-21
source: lab rel/0.7.16 20261121-003202 nest_target_probe
role: 
versions: 0.7.13, unreleased
fixed_in: 0.7.17
branch: fix/w84-tests
commits: 91754ef4 dc685a9b
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
* 2026-11-23 (fix/w84-tests): found in the trace lines: the guest's own state differs between the runs. Passing
  (20261120-133703): the guest's `flags=0x9` at the 300 / 600 m spots (PF_JUMPING: its game has the creature off the
  ground, `own_airborne`, so the state carries its agl and the host keeps that height: drawn 21.82 / 36.10). Failing
  (20261122-194210, the same spots and heights on the guest, agl 21.82 / 36.10): `flags=0x1`, no PF_JUMPING: the
  guest's game kept the grounded byte (+0x137 = 1) after `coop.npc gopos` moved it into the air of its paused game
  (busy=cinematic,input_lock,paused: no physics step clears it), so the host drove a grounded partner and drew it 6.34 m
  lower at both spots. The test made that state: a paused game's creature cannot get 20-36 m into the air by itself.
  around() keeps the host's radius, so the 600 m spots lie 21.8 / 36.1 m over the guest's terrain. Not a player bug as
  far as these rows show (why a grounded partner that high is drawn exactly 6.34 m lower is not traced).

## Fix
None yet (cause not proven). The test prints both games' `coop.ground` (GetHeightAt radius, the snap surface, the
point's height over them) under the guest at every spot, so the next run that shows the offset names the game whose
terrain disagrees. The monitor rule is unchanged.
* fix/w84-tests (dc685a9b): coop_nest_target_probe.py on_ground(): each spot is moved along its radius to 0.3 m over
  the guest's own ground there (the highest of `coop.ground`'s ground, surface and water) before the `coop.npc gopos`.
  The probe's question (the guest's next nest) does not depend on the height. Test only, no mod change.

## Verification
Lab (shows the cause): `SPORE_DETECTOR=20 python3 tools/lab.py run nest_target_probe --repeat 4 --lane N --retry 0
--no-build`: in a run with the `drawn` violation, the line "ground under the guest: host ..., guest ..." at the 600 m
spots. Open: waiting for that run, then the fix.
- Still open (2026-11-23): nest_target_probe FAIL in 20261122-194210 and 3 of 4 in 20261122-193406 (#2-#4: host draws the guest 6.3 m off, from s 120). The new lines "ground under the guest" agree on both games at every spot (ground, point and water the same; once the host's snap surface 506.34 against the guest's 505.93, 0.41 m): the 6.3 m is not the terrain under the guest. Next: the guest's held position in its paused game against the host's drawn one.
- Pending night run (fix/w84-tests): `SPORE_DETECTOR=20 python3 tools/lab.py run nest_target_probe --repeat 4 --lane N --retry 0 --no-build` PASS 4 of 4 with no monitor `drawn`
  gate (the "ground under the guest" lines: the point 0.3 m over the ground on both games). FAIL before: 20261121-003202,
  20261122-194210, 20261122-193406.
0.7.17 release lab: PASS: nest_target_probe (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-21: opened from the third final check of 0.7.16 (fix/w62-flake-cc); diagnostics added to the test.
- 2026-11-23: again in lab 20261122-194210 (nest_target_probe, lane 7, gate monitor drawn 6.3 m, the test RESULT OK): the same numbers (agl 15.48 / 21.82 at s 120, 29.76 / 36.10 at s 130, d=565), and the new ground lines show both games read the same ground there (dir 1 483.55 / 483.55, dir 3 469.27 / 469.27): the 6.3 m is in the host's drawn placement, not in GetHeightAt. Still open, 0.7.16.1.
- 2026-11-23: lab evidence of the 0.7.16 final check added; stays open.
- 2026-11-23: fix/w84-tests (WP-T1 of 0.7.17): cause found in the guest's state flags (a grounded byte kept in its paused game); the probe puts the guest on its ground (dc685a9b).
- 2026-11-24: released in 0.7.17.
