---
id: BUG-1366
title: Tests: creature_mating_far still gives the host's game 30 s to pick a mate (BUG-1182 left it out)
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: final-check-20261120-133703
role: 
versions: unreleased
fixed_in: 0.7.16
branch: fix/w61-creature-c
commits: cbd81627
tests: creature_mating_far (tools/tests/coop_creature_mating_far.py)
signature:
docs:
related: BUG-1182, BUG-0815, BUG-1112
---

## Report
Second final check of 0.7.16 (lab 20261120-133703, lane 4, rel/0.7.16 673f7a55): creature_mating_far FAIL at its first
step, "the host's game picked a mate near the nest" and "the mate is one of the host's NPCs (None)", 60 s in all. The
same run's creature_mating (lane 3) passed (`mate 1173F1B0 flags=10`). History: 0 of 3 failed before, all of them
before the lab's condition start-up waits.

## Root cause
Observed (tools/tests/coop_creature_mating_far.py): the probe waits 30 s for the host's game to name a mate
(`coop.drama mate`, flags 0x10). BUG-1182 (0.7.15) found that since the lab's condition start-up waits (A9, BUG-1112)
the test starts some 7 s sooner after the load and the game had picked no mate within 30 s; it raised the wait of
coop_creature_mating.mate_click to MATE_WAIT_S = 60 s (creature_mating, editor_now, npc_editor_hold), but this probe
has its own loop with the old 30 s and was not changed. Not a mod bug and not the probe's subject (BUG-0815: the mate's
reply radius); the run never reached the call.

## Fix
coop_creature_mating_far.py waits MATE_WAIT_S (60 s, `MATING_WAIT_S=30` the old wait) as BUG-1182, and prints the
mate line with the time it took. Test-only.

## Verification
FAIL (30 s): lab 20261120-133703 creature_mating_far (lane 4). PASS: waiting for the lab run (no game today, owner
rule): `SPORE_DETECTOR=20 python3 tools/lab.py run creature_mating_far --lane N --retry 0 --no-build`. Open, waiting for
the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): creature_mating_far PASS in 20261121-003202 and 20261122-194210 (FAIL before: 20261120-133703). Closed.

## History
- 2026-11-20: opened from the 0.7.16 second final check (fix/w61-creature-c); the wait of BUG-1182 applied.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
