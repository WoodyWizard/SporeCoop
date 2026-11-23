---
id: BUG-1349
title: tribe_mirror_hold: the host's chief lets go of the staff at once in every round of some runs (the test's hand lever)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-20
source: lab
role: 
versions: rel/0.7.16
fixed_in: unreleased
branch: fix/w60-tribe
commits: b9ce678f 063376e4
tests: tribe_mirror_hold
signature:
docs:
related: BUG-0914, BUG-0992, BUG-1347, BUG-1335
---

## Report
The 0.7.16 final check (lab 20261120-041304, lane 2): tribe_mirror_hold "FAIL the host's chief held its staff in its
animation (0 rounds >= 2)": all 12 rounds "the host's chief let go at once" (anim 02481DE5 nine times, 04866D8E,
04866DAB, 02796B31), and no swing key frame (the chief held nothing by itself in 60 s). The same in feat/w51-tribe
20261115-182010 (12 of 12, swing key frame taken). `lab.py flaky`: 10 of 32 runs since 2026-11-12 (most of the older
ones were the guest's copy, fixed since: BUG-0968, BUG-0992). The natural holds of the other members were relayed
(24 of 29) in the same run: the subject of the test was fine, its lever on the host's chief did nothing.

## Root cause
Not proven from the logs (the host's game log has no hold lines; the test printed nothing but the animation).
* ✔ (all tribe_mirror_hold logs in the worktrees) the lever's outcome by the chief's animation at the attach: 02481DE5
  held 25 times and let go 30 times, 04C6CB02 held 37 / let go 8, 04866DAB 6 / 9: the same animation both ways, so it
  is not the animation. Runs are all-or-nothing more often than chance (two runs 12 of 12).
* ~ (inferred, BUG-0992's ✔ mechanism, docs/re/tribe.md#tribe-anim-queued-start) the attach binds the hold to a pending
  track of the drawn id first; a hold on a track that never plays ends at once. A chief whose behaviour keeps queueing
  its idle would lose every attach. Not checked at runtime.
* The mod's own paths are not in this lever (it is the game's SetHandheldItem on the host's own chief); since
  creature.fx_bone (BUG-1335, merged after this run) an attach on a bone outside the body is not made: the round's
  diagnostics now print `handbone` (state, fx_skips) to rule that out.

## Fix
Test lever and test (no change for players): `coop.tribe hand <id> <item> pending` ends the pending starts of the
drawn animation first (the game's call of BUG-0992, `pending_cleared=`); tribe_mirror_hold uses it, makes up to 3
attaches per round, each watched 1.5 s for the game's `hold=1` (a state, not one sample 1.5 s later), and prints for a
round that still lets go: each attach's animation / held / cleared, the chief's `hand` line and its `handbone` line.
The checks (rounds >= 2, the guest's copy follows each, puts it back) are unchanged.

## Verification
Was waiting for the lab run: tribe_mirror_hold must PASS; if a round still lets go, its printed attaches and
handbone line say whether pending tracks were there (cleared=1) and whether creature.fx_bone refused the bone
(fx_skips).
`SPORE_FIX_OFF=tribe.mirror_hold` (by hand, BUG-0914's twin) must still FAIL on the guest's copy. If it fails again with cleared=0
and no fx_skips, this ticket gets a runtime probe of the chief's modifier table (cause still open).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): tribe_mirror_hold PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643 (the FAIL before: the final check named in the Report). Closed.

## History
- 2026-11-20: opened.
- 2026-11-20: cause and fix on fix/w60-tribe (no lab today, owner rule); lab run pending.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
