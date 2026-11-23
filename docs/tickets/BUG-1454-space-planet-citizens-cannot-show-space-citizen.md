---
id: BUG-1454
title: space_planet_citizens cannot show space.citizen_lead: it reads 19 mostly standing citizens 3 times, the switch acts only on runners; the twin passed 4 of 4
status: open
kind: test
stage: Space
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in:
branch: fix/w84-tests
commits: 15a93727 647e2cf9 60a851e9
tests: tools/tests/coop_space_planet.py (space_planet_citizens, new space_planet_runners)
signature:
docs:
related: BUG-1404, BUG-1210, BUG-0490
---

## Report
Night script of the fourth 0.7.16 final check, out/night-0.7.16-d/1-fixoff-citizens.txt (lab 20261122-192851):
`SPORE_LAB_FIXOFF="space_planet_citizens=space.citizen_lead" lab.py run space_planet_citizens --repeat 4` PASSED 4 of 4;
at least one FAIL was expected (BUG-1404).

## Root cause
Test (it cannot see the switch) and BUG-1404's claim. The fix-off was applied (`SPORE_FIX_OFF: space.citizen_lead off
BUG-1404` in each log, `coop.fix space.citizen_lead off` on both games) and the path ran (19 citizens bound and driven,
rx=62 in ~31 s: the 0.5 s tick). The switch changes only the short error of a runner (~5 m/s) that starts, stops or
turns; the case reads 19 mostly standing citizens 3 times and fails at 3 of 57 over 3 m: in the 4 twin runs 0 of 57.
Since BUG-1210 at 0.5 s snapshots: 9 runs, 2 FAIL (20261121-003202 4 of 57, 20261117-154911 6 of 57), 11 misses in 513
samples: ~22 % a run, so 0 FAIL in 4 runs is ~37 % likely. BUG-1404's "fails about half the runs" came from before
BUG-1210 (unbound citizens 7-31 m off, a different cause); the "runners" cause is inferred (no speeds recorded).

## Fix
coop_space_planet.py: the citizens case's docstring corrected; a new case `--case runners` (suite
`space_planet_runners`): after the landing it waits (up to 240 s) until 2 host citizens run near the guest, then 60
rounds of host, guest, host dumps; each host citizen (by serial) over 2 m/s between its two host reads is taken at the
guest's moment and compared with the nearest guest citizen; FAIL over 10 % of the runner samples over 2.5 m;
INCONCLUSIVE under 40 samples. The limits (RUN_SHARE 0.10, RUN_OFF 2.5) are provisional, from the mechanism: a
measuring run of both arms sets them before "SPORE_FIX_OFF=space.citizen_lead fails" stands (fixcheck lists it
unverified until then). The citizens case's 3 m / 5 % checks are unchanged.
* fix/w84-tests (60a851e9): the fix-on arm was measured on rel/0.7.16.1 30be883d (lab 20261123-111347, 4 PASS: 443 /
  211 / 355 / 429 runner samples, p50 0.47-0.48 m, p90 0.90-1.28 m, max 2.23-3.62 m, 0 / 3 / 0 / 0 over 2.5 m, at most
  1.4 %): RUN_SHARE 0.10 -> 0.05 (a 3x margin over the on arm). Each run now also prints the share over 1.0, 1.5, 2.0,
  2.5 and 3.0 m, so the fix-off arm's run shows where the two arms part. Test only, no mod change.

## Verification
Open, waiting for the lab run (measure first): `SPORE_DETECTOR=20 python3 tools/lab.py run space_planet_runners
--repeat 4 --lane N --retry 0 --no-build` (PASS expected) and `SPORE_LAB_FIXOFF="space_planet_runners=space.citizen_lead"
SPORE_DETECTOR=20 python3 tools/lab.py run space_planet_runners --repeat 4 --lane M --retry 0 --no-build` (FAIL
expected); read both arms' "runner samples ... p50 / p90" lines and set the limits between them. Without the new case
the old twin needs ~12 repeats to mean anything.
- Still open (2026-11-23): space_planet_runners INCONCLUSIVE on fd28f88f in every run, fix on and off: 20261123-021900 (2 of 2), the twin 20261123-022402 (2 of 2), 20261123-023643 (3 in a row) and 20261123-071312 (4 in a row), each "0 runner samples (at least 40): no host citizens ran near the guest". The case never came, so the limits are not set and BUG-1404 stays unproven. Next: a way to make host citizens run near the guest (a planet or spot where they do, or a lever).
- 2026-11-23 (fix/w84-tests): PASS arm measured: 20261123-111347 space_planet_runners#1..#4 (rel/0.7.16.1, the 0.15 s
  reads of 647e2cf9: the case comes now). Pending night run (the FAIL arm): `SPORE_LAB_FIXOFF="space_planet_runners=space.citizen_lead"
  SPORE_DETECTOR=20 python3 tools/lab.py run space_planet_runners --repeat 4 --lane N --retry 0 --no-build` FAIL expected (read the "runner samples over" lines; if the off arm stays
  under 5 % over 2.5 m, pick the limit between the arms from those lines), and `SPORE_DETECTOR=20 python3 tools/lab.py run space_planet_runners --repeat 2
  --lane M --retry 0 --no-build` PASS with the new 5 %.

## History
- 2026-11-23: opened; fix/w65-tests: the claim corrected, a runners case added.
- 2026-11-23: lab evidence of the 0.7.16 final check added; stays open.
- 2026-11-23: validation 20261123-023643: space_planet_runners INCONCLUSIVE "0 runner samples" both ways (2 + 2). Inferred from the timing (60 rounds, the wobj read and the fault checks in ~18 s): the 3 reads of a round ran back to back, the two host reads under the 0.05 s guard, every round dropped (not measured: no per-read times). fix/w65-tests: the reads of a round 0.15 s apart, and the wait prints what the rounds saw (reads apart, matched by serial, moving, fastest), so the next INCONCLUSIVE says whether the citizens stand.
- 2026-11-23: fix/w84-tests (WP-T1 of 0.7.17): on arm measured (20261123-111347), RUN_SHARE 5 %, share table printed (60a851e9); off arm pending.
