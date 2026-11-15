---
id: BUG-9194
title: Creature: the evolve cocoons are doubled (two shells on each creature)
status: fixed
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 64c3afed a8a3ceca
tests: tools/qa_win (TITAN, the partner's radius sampled through the cocoon, 2026-11-14)
signature:
docs:
related: BUG-9181
switch: creature.cocoon_one_spot
---

## Report
Owner live test 2026-11-13 20:31-20:55 (their own PC hosting Vandh.spo in Creature; two test guests from the MSI joined late: MSIGuest with a creature, MSIJoiner without one, editor first). "I also noticed that the cocoons are doubled" (a screenshot: a second shell overlapping one cocoon).

## Root cause
Inferred (code): keep() lays a new woven shell every kShellEvery 1.8 s, each living ~2.0 s, at the creature's position then; the overlap of a held puppet's drifting position showed two shells side by side, at the owner's frame rate (the earlier checks ran at 10 FPS).

## Fix
creature.cocoon_one_spot (on): every shell and sac of one cocoon at the spot it went in (Cocoon::spot).

Second fix (2026-11-14, a8a3ceca): evolve.cpp: while going in, the shell is laid again on time and the creature shrinks with the spin from its size to kShrunk over kEnterSeconds; the step to the hold keeps the body and its shell timer (no second shell); the family is moved back only when it wandered 1.5 m while going in (the spin's hop). Switch: `coop.fix creature.cocoon_smooth off`.

## Verification
no-fail-run: not run again live yet (the owner tests the build on their install).

Verification 2 (a8a3ceca, TITAN, the guest's body sampled on the host every 0.1 s as the guest opened its editor): FAIL (switch off): radius 1.71 for 2.4 s, then 0.20 in one step. PASS (switch on): 2.04 down to 0.24 in even steps over 2.4 s, no jump. Not filmed: the camera could not be held on the guest (the shells' count is from the code path).

## History
- 2026-11-13: opened.
- 2026-11-14 (evening): reopened. Owner play test (3 players): still two cocoons, and the creatures stand still instead of the little spin jump. Cause found in code (high confidence): the first `wrap(fresh=true)` shell ends at 2.0 s with the creature not shrunk; at kEnterSeconds 2.4 s the step to holding clears `c.puppet` and calls `keep()`, which treats it as a new body and lays a second shell (`wrap(fresh=false)`) while shrinking to 0.12 at once (evolve.cpp around 148, 170, 213, 315). The 64c3afed fix only stopped the shell from drifting. The spin is played but overridden by the members' AI or sync (`family_keep` moves them back every tick).
- 2026-11-14 (night): fixed again (decision 3V A), the shrink measured live on TITAN; the spin itself is still to be seen on the owner's PCs.
