---
id: BUG-1228
title: Civ: a guest's copy of a standing wild creature on a steep slope of the guest's ground is put back every 2.5 s (trail teleports, 20 m/s)
status: open
kind: desync
stage: Civ
reported: 2026-11-17
source: BUG-1216 analysis (final check 0.7.15, lab 20261116-025154 campaign_tc2)
role: guest
versions: rel/0.7.15
fixed_in:
branch:
commits:
tests:
signature: "sync-check:   nNNN here: ... own=19-22 ... trail=N" growing on a guest while the host's creature stands
docs: docs/re/civ.md#civ-npc-ground
related: BUG-1216, BUG-0305, BUG-0402
---

## Report
Found in BUG-1216's analysis. In the 0.7.15 final check (lab 20261116-025154 campaign_tc2, s 1363-1397 of the run, a
live Tribe -> Civ campaign) a herd of species 067A147A stood still on the host (spd 0, agl 0.00). On every guest the
ground at that spot lies 9.2-10.5 m higher (`want_agl=-9.24 / -10.47`) and slopes steeply: Carol's copies n404 / n406
stood 2 m beside their target on their own ground (agl 0.00) but 1.3-1.8 m lower than the ground at the target, moved
at 19-22 m/s (`own=`) and were trail-teleported back every ~2.5 s (`trail=6 -> 20`, `tp=7 -> 20` in 33 s, `nudge`
pulls in ~97 % of the frames). A player on such a guest would see the animals slide and pop back in place.

## Root cause
Observed: the guest's terrain at the host's spot differs (10 m higher, a ~40 degree slope over 2 m: the target on the
guest's ground at 523.35 m from the centre, the copy 2 m away at 521.6 m with agl 0). Inferred: the copy cannot stand
where its target is on that slope (it slides down, or something solid the terrain query does not know stands there);
drive::step then pulls (nudge) and trail-teleports it (BUG-0305's rule: over 1.5 m off for 0.5 s) again and again.
Why the host's live world and the guests' copy of its save differ in height there is not known (no terrain stamp
within 250 m). Not seen in the segment runs (every game loads the same save).

## Fix
Open. Candidates: a standing copy that keeps sliding off a target the guest's ground does not hold is left where it
comes to rest (within a few metres, no trail teleports for a standing remote); or find why the terrain differs
(a live Tribe -> Civ host against guests that load its save).

## Verification
Reproduce: a 4-player campaign through a live Tribe -> Civ (campaign_tc2); the segment runs from the saved Civ do not
show it.

## History
- 2026-11-17: opened.
