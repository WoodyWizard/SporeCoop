---
id: BUG-0992
title: Tribe: a short host hold in the change animation is missed on the mirror while the mirror's gesture runs on (the relayed start waits behind it)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-12
source: 0.7.13 final check tribe_hold_guest, lab 20261112-175447 / 20261112-205503
role: guest
versions: 
fixed_in: 0.7.13
branch: fix/w48-ftribe
commits: 32685d83
tests: tribe_hold_guest
signature:
docs: docs/re/tribe.md#tribe-anim-queued-start
related: BUG-0968 BUG-0971 BUG-0914 BUG-0993
---

## Report
0.7.13 final check: tribe_hold_guest failed about 1 run in 3 after BUG-0968 / BUG-0971: "the host member's holds are
the mirror's too (11 shared, missed [(30.4, 0.35, '02C39200')])" (out/lab/20261112-175447 in the rel/0.7.13 worktree)
and "missed [(33.2, 0.43, '02C39200')]" (20261112-205503 #2). This branch's runs also missed long holds in the gesture
0x04C6CB02: 4.2 s (fix/w48-ftribe out/lab/20261112-213048 #1) and 2.7 s (20261112-214601 #2). A guest sees the host's
chief without its staff in the hand while the host sees it held.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (holdtrace, Ghidra; docs/re/tribe.md#tribe-anim-queued-start):
(a) a start over a non-looping current animation waits until that one ends (CreaturePlayAnim gives the new track mode
0). The host's own change started 1.1 s after its tree's call; the mirror's gesture began later than the host's and so
ended later: the relayed start and hold_try's start (`R` / `P 02C39200 04C6CB02`) did not change the mirror's
animation before the host's 0.35-0.43 s hold was over (`t` until `E`).
(b) the attach 0xC240E0 binds the hold modifier to the track of the current id found by 0x9FF700, which looks in the
pending tracks first: a relayed restart of the gesture the mirror played was a pending track of that id, the hold
went to it and its tick ended it (`T 04C6CB02 1100` once a second, no hold, for 2.7-4.2 s).
Not a relay-tick limit: the 0.25 s tick binds within 0.25 s; the shortest missed hold was 0.35 s.

## Fix
mods/coop/tribe.cpp hold_try, switch `coop.fix tribe.hold_queued` (on; SPORE_FIX_OFF=tribe.hold_queued: the old path):
(b) before the attach, when a pending track has the mirror's current animation id, the pending tracks are ended with
the game's own call (SporeCoop::AnimatedCreature_ClearPending 0xA02B00, as CreaturePlayAnim does for its `a`; trace W).
(a) a fresh host hold (under 1 s) whose animation the mirror still does not play 0.2 s after hold_try started it is
held in the animation the mirror plays (trace Q); the host's end ends it as any bound hold.
(Tried first and dropped: starting the hold animation with cCreatureBase::PlayAnimation; in case (b) it only added one
more pending track: 20261112-214601 #2.)
switch: tribe.hold_queued

## Verification
Test `tribe_hold_guest` (tools/tests/coop_tribe_hold_only.py --who guest --windows 2). The miss is the game's timing
(about 1 run in 3 without the fix), so the FAIL runs are the code without the fix and the branch's partial versions:
* FAIL (rel/0.7.13 code, no fix): out/lab/20261112-175447 and out/lab/20261112-205503 #2 in the rel/0.7.13 worktree
  (case a, 0.35 / 0.43 s in 0x02C39200).
* FAIL (only part a, or part b tried with PlayAnimation): out/lab/20261112-213048 #1, out/lab/20261112-214601 #2
  (case b, 4.2 / 2.7 s in 0x04C6CB02).
* With SPORE_FIX_OFF=tribe.hold_queued: out/lab/20261112-221226 passed 3 of 3 (no gesture restart or late change came
  in those runs): the switch alone does not reproduce on demand.
* PASS (final code): out/lab/20261112-220213 (3 of 3), out/lab/20261112-222703 (3 of 3), out/lab/20261112-222723
  (the shared-holds check 3 of 3; #3 failed the separate own-hold rule at 26 fps, BUG-0993). 0 misses in 9 runs
  against 4 in 13 runs without the complete fix. Related: tribe_mirror_hold and same_view_tribe PASS
  (out/lab/20261112-220233; its hold key frame shows the staff the same way on both games).

## History
- 2026-11-12: opened.
- 2026-11-12: fixed on fix/w48-ftribe (tribe.hold_queued).
- 2026-11-12: released in 0.7.13.
