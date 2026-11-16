---
id: BUG-1205
title: Tests: tribe_caption_race fails when the host's game itself plays no conquest caption (the scenario did not happen)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-16
source: final check 0.7.15 (lab 20261116-025154)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w51-t4
commits: d9c70eee
tests: tribe_caption_race (coop_tribe_conquest_guest.py --click-at-close)
signature: FAIL the guest's game played the conquest caption
docs: docs/06-testing.md#testing-final-triage-tribe-civ-0715
related: BUG-1101, BUG-0777
---

## Report
Final check 0.7.15 (lab 20261116-025154, rel/0.7.15 11114769, lane 3, warm): `the guest's game played no caption within
40 s of the fall` / `FAIL the guest's game played the conquest caption`. The same twice in the 0.7.14 triage
(fix/w49-fcgates lab 20261114-031126 #1 and #2, cold). Ledger: 3 of 19 runs failed since 2026-11-14.

## Root cause
Observed: in all three failed runs the host's game played no conquest cinematic either (no `the village cinematic
... plays on the host (BUG-0531)` line in the host's log, no `watches a cinematic`), while in every passed run both
games played TRG_TotemPole_Conquer (0x5418B038) about 8 s after the fall, then TRG_Upgrade. The guest's own conquest
ran as it should (`a hut falls here ... state 2`, `the village ... was conquered in the host's game: here too`, the
village fl=2 on both). So the game itself left the caption out on the host, and the guest had none to play; the
test's subject (a click at the caption's forced end, BUG-1101) did not happen. Not related to a 0.7.15 merge (0.7.14
twice). Why the game leaves it out now and then is not known (inferred: the raid is ordered by `coop.tribe order`,
not by the host's click; docs/re/tribe.md#tribe-raid-end saw a conquest without the host's click play no cinematic).

## Fix
coop_tribe_conquest_guest.py --click-at-close also watches the host's busy state while it waits for the guest's
caption; when neither game played one, the conquest checks still run and the result is `RESULT: INCONCLUSIVE` (A15:
the precondition did not happen; never a pass, three in a row fail the run). A caption on the host but none on the
guest still FAILS as before. No mod change.

## Verification
* FAIL (the old test, the scene: no caption on either game): 20261116-025154:tribe_caption_race (rel/0.7.15), and
  fix/w49-fcgates 20261114-031126 #1 / #2 (0.7.14).
* PASS (the new test, a caption on both games): 20261116-091255 tribe_caption_race#1 / #2 (fix/w51-t4); the old test
  on the same commit 20261116-082817-2 #1 / #2.
* Not seen: the new INCONCLUSIVE path at runtime (the game's left-out caption cannot be planted; 3 of 19 runs). Its
  condition is the host's `coop.status` busy state, read by the same helper as the guest's.

## History
- 2026-11-16: opened and fixed (fix/w51-t4, final-check triage 0.7.15): the test's verdict, no mod change.
