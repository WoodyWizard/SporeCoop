---
id: BUG-0989
title: Creature: a joining guest's creature appears at the host's creature's exact spot (drawn inside each other, other poses on each game)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-12
source: rel/0.7.13 final check out/lab/20261112-175447 (key-frame review)
role: 
versions: 0.7.13-rc
fixed_in: unreleased
branch: fix/w49-mirror
commits: 0db432fb
tests: creature_join_apart, play_creature
signature:
docs: docs/re/creature.md#creature-join-apart, docs/known-issues.md#issue-join-same-spot, docs/06-testing.md#testing-t2-mirror
related: BUG-0340
switch: core.join_apart
---

## Report
Key-frame review of the rel/0.7.13 final check (lab out/lab/20261112-175447): on the creature fixture both
games show the host's and the guest's creatures at one point, drawn inside each other, from the first frame on
(041 play_creature start, 031 find_layout, 034 menu_splash, 050/053 same_view: both avatars at
(-171.8 -467.8 -84.0) in the manifests). In 050 and 053 the two bodies have other poses on the two games (on the
host Bob stands upright on Alice / lies over her, on the guest Alice stands upright in front of him).

## Root cause
Confirmed (a rejoin on the creature fixture with the fix off): the guest's creature starts where the host's save has
the player's creature, 0.06 m from the host's creature, and stays there (0.08 m on both games for 20 s). The two bodies
are drawn inside each other and each game poses them on its own; the host's name tag stands over that spot.

## Fix
Once per arrival in Creature, when the host's puppet is first within 2.5 m of the guest's creature for 0.5 s (within
30 s), the guest's creature is put 5 m beside it on the ground and surface. Switch `coop.fix core.join_apart off`.

## Verification
Test `creature_join_apart` (coop_creature_join_apart.py: a rejoin; 15-20 s later the two creatures >= 3 m apart on
both games).
FAIL (SPORE_FIX_OFF=core.join_apart): out/lab/20261113-051156 (0.08 m). PASS: out/lab/20261113-051156-2 (5.07 m / 5.01 m;
key frame: side by side), and the final check runs (see History).

## History
- 2026-11-12: opened.
- 2026-11-12: re-check key frames (out/lab/20261112-225741, sheet 013): at the guest's join ('loading the world of Alice') the guest's tag 'Alice' hangs over the guest's own creature for a moment, Alice's creature stands to the left; right on the next frame (014). Likely the same spawn point cause.
- 2026-11-13: fixed on fix/w49-mirror (package T2 of 0.7.14).
