---
id: BUG-0326
title: tests: sync_lowfps fails 'civ/vehicles: 90% facing within 20 deg' on main d003127 (lane 4, 2 of 2)
status: fixed
kind: test
stage: Civ
reported: 2026-10-30
source: fix/w14-creature lab 20261030-163146, 20261030-163902
role: 
versions: 
fixed_in: 0.7.3.2
branch: fix/w15-civ
commits: 70bed48
tests: sync_lowfps
signature:
docs: docs/06-testing.md#testing-sync-lowfps-round
related: BUG-0175
---

## Report
`tools/lab.py run sync_lowfps --lane 4` on fix/w14-creature (main d003127 + Creature-only changes):
FAIL "civ/vehicles: 90% facing within 20 deg" in out/lab/20261030-163146 and again with that branch's
switches off (npc.kinematic, creations.editor_after_swap: out/lab/20261030-163902), so not caused by
it. BUG-0175 (fixed) had this suite passing on 2026-10-30 (20261030-133141). Not analysed (Civ is
another agent's area): the facing of the guest's vehicle mirrors at 15 fps.

## Root cause
The test (observed, lane 7, `SYNC_VERBOSE=1 coop_sync.py --fps 15 civ`, per-sample lines):
* In `--fps` mode `stir()` gives the vehicle a new move order every sample, alternately 40 m away
  from and back towards its city: the host's vehicle turned 179 deg within the 0.15 s comparison
  window at every sample (15 of 16).
* The facing check compares the guest's pose with the host's turn between its samples by nlerp; for
  a ~180 deg turn the direction of the turn is undefined, so a guest pose caught mid-turn (the guest
  shows the host's pose ~0.1-0.2 s later) read 26-80 deg off in 3-6 of 16 samples: p90 38-87 deg.
* Not caused by civ.extrap / civ.mirror_place / civ.vel_cap: the same with all three off (p90 38.7,
  max 80.5 deg); not by the Creature branch either (its runs with its switches off). The BUG-0175
  run that passed (20261030-133141) was a lucky draw: the suite's history shows p90 1-87 deg for the
  one vehicle (16 samples, two bad ones fail it).
With a drive round the city (no reversal) the host turns 3-57 deg per window and the guest's facing
is within 0-10 deg (p90 1.2-4.1).

## Fix
`coop_sync.py` (70bed48): in `--fps` mode the vehicles drive round their city (a target 35 deg further
on a 45 m circle each sample) instead of away / back; `SYNC_VERBOSE=1` prints each vehicle sample's
facing error and the host's turn over the window. No game code changed.

## Verification
* FAIL (old test): out/lab/20261030-163146 and 20261030-163902 of fix/w14-creature (p90 70.0 and 77.3
  deg); direct runs on lane 7 (main c69fb42): p90 73.6, and 38.7 with civ.vel_cap, civ.extrap and
  civ.mirror_place off.
* PASS: lab run 20261030-172656-fix-w15-civ (sync_lowfps 3/3, vehicle facing p90 4.1 / 1.4 / 1.2 deg).

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w15-civ): a test problem (180 deg flips every sample), test fixed.
- 2026-11-01: released in 0.7.3.2.
