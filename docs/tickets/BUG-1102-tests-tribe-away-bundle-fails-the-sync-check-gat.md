---
id: BUG-1102
title: Tests: tribe_away_bundle fails the sync-check gate whenever a check falls into its forced host-away rounds (the guest plays the tribes alone on purpose)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-14
source: final check 0.7.14, lab 20261114-005006 tribe_away_bundle
role: both
versions: rel/0.7.14 4dd8d2be
fixed_in: unreleased
branch: fix/w49-fcgates
commits: 211a9ef6
tests: tribe_away_bundle
signature: tribe_away_bundle: gate sync-check "t: N apart (t5 44.0 t6 33.4)" in the coop.awaytest rounds
docs: docs/06-testing.md#testing-fcgates-0714
related: BUG-1084, BUG-1089
---

## Report
The 0.7.14 final check (lab 20261114-005006, lane 4) failed tribe_away_bundle by the lab's sync-check gate: `MISMATCH
with the host (Tribe A3930F03, 9 objects compared): t: 2 apart (t5 44.0 t6 33.4)` at s 49, while the test's own checks
all passed; the rerun (20261114-022615) passed.

## Root cause
Observed (logs of 23 recorded tribe_away_bundle runs, Nov 11-14): the test makes the guest count the host as away
(`coop.awaytest 1`, the editor stand-in) while the host's game still plays Tribe. In that state the guest runs the NPC
tribes alone on purpose (t5 `ours=0` on the guest), so the two games' members walk apart: 17 of the 23 runs logged a
sync-check MISMATCH of 1-4 citizens 10-120 m apart in those rounds (every one failed or passed only by the check's
timing). A real away host is never compared (it is in an editor or the menu: other place). Not a mod bug: a difference
the test makes and did not mark.

## Fix
coop_tribe_away_bundle.py wraps each `coop.awaytest 1` .. `0` round in `LAB-MARK sync-force on/off`
(twoinst.lab_mark), the window tools/loggates.py leaves out of the sync-check gate (as cell_churn and cell_pause do).

## Verification
no-fail-run: test only (the gate's window); the MISMATCH comes whenever the 30 s check falls into an away round
(17 of 23 recorded runs). PASS: out/lab/20261114-033453 (lane 7: the host logged a MISMATCH inside a marked away
round, 4 LAB-MARK lines, the gate passed).

## History
- 2026-11-14: opened and fixed on fix/w49-fcgates (final check 0.7.14 triage).
