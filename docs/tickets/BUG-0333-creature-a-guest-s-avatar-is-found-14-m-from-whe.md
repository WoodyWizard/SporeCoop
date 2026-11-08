---
id: BUG-0333
title: Creature: a guest's avatar is found ~14 m from where it joined, at the same spot (-159 -471 -86) in 2 cold team runs
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-10-30
source: fix/w17-creature, BUG-0330 analysis
role: guest
versions: 
fixed_in:
branch: fix/w46-p4-creature
commits:
tests: guest_spot, guest_spot_swap (tools/tests/coop_guest_spot.py)
signature:
docs: docs/known-issues.md (Creature: the guest's avatar ~14 m from where it joined)
related: BUG-0330
---

## Report
In 2 of ~60 `team` runs in all lab history the guest's avatar stood ~14 m from where it joined, both
times at the same spot: out/lab/20261030-205830 (smoke, cold, all fixes on: guest trace (-158.5 -471.6
-86.4) 10 s after the load) and fix/w17-creature out/lab/20261030-230659 team#5 (cold,
`npc.kinematic` off: (-159.0 -471.4 -86.5); 0.9 m from the host before the joint hunt, 9.8 m before
the regen check). Both times the guest's own creature had been put on its avatar (SetSpecies,
"creations: own avatar is now 290225CD") a few seconds before.
Not the debug hit (a hit on proxies 14-37 m away did not move the guest), not the editor wait, not
npc.kinematic. Players would see their creature jump 14 m (inferred; no screenshot).
Reproduce: `tools/lab.py run team --repeat 10` cold and look for "players N m apart before the
regen check" with N > 5 (the team test now puts the guest back, it does not fail on it any more).

## Root cause
Not found; not reproduced on rel/0.7.11 (fix/w46-p4-creature, 2026-11-07). Observed:
* Both runs of the report were 0.7.3.1, cold, and in both the guest's own creature (Kirgle 290225CD) was put on its
  avatar 3.0 s after the load (SetSpecies, "own avatar is now 290225CD"); the move came during the test's joint hunt
  (team#5: 0.9 m from the host before it, 13 m 3 s later), the NPCs then 13-14 m away. The spot is the one a
  click on the ground ahead of the default camera reaches: the other lab runs found at (-158..-159 -471 -86) are
  tests that walk the guest with clicks / W (select, puppet_air, latency_step_ingame), none without input.
* Since BUG-0345 / BUG-0638 a guest no longer plays its newest own creation in a world it never played it in: in
  every `team` run since 0.7.6 there is no swap at all (0 "own avatar is now" lines, the guest plays the world's
  species), so the report's path is not the default any more. Forced with `SPORE_FIX_OFF=creations.guest_world_species`
  (the old default, the swap 3 s after the join) the drift did not come back: team 6/6 (out/lab/20261107-204325: the
  guest 0.7-1.4 m from the host before the hunt, after the guest's hit and before the regen check), guest_spot_swap
  5/5 and guest_spot 5/5 (out/lab/20261107-205132: the swap in every _swap run; the guest's own position sampled 4x a
  second for 31 s while it and the host hit 4 wild NPCs at 14-56 m: at most 0.4 m from where it joined).
* Inferred (not verified): the ability bar left from the world's species after the swap (BUG-0358, fixed 2026-10-31,
  after both runs) or an input the test harness of that time sent; neither is in the current build's path.

## Fix
None (not reproduced). The reproducer `coop_guest_spot.py` (suite guest_spot, guest_spot_swap) samples the guest's own
position through the team test's steps, with and without the own creature swap.

## Verification
no-fail-run: not reproducible on rel/0.7.11: team 6/6 with the swap forced (out/lab/20261107-204325), guest_spot 5/5 and
guest_spot_swap 5/5 (out/lab/20261107-205132), the guest at most 0.4 m from where it joined.

## History
- 2026-10-30: opened.
- 2026-11-07: fix/w46-p4-creature: a dedicated attempt on rel/0.7.11 (the swap forced, a sampling reproducer, 16 runs): not reproduced; the report's path (the newest own creation swapped on in any world) is no longer the default.
