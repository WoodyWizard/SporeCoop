---
id: BUG-1237
title: Creature: a guest's own creature is drawn plain green for about 2 s after a change (model not baked yet?), candidate from key frame S/050
status: open
kind: bug
stage: Creature
reported: 2026-11-18
source: key-frame review of final check 20261117-154911
role: 
versions: unreleased
fixed_in:
branch: feat/w74-captain
commits:
tests: own_prebake, own_prebake_off
signature:
docs: docs/re/creations.md#creations-captain-0717, docs/re/creations.md#creations-model-bake
related: BUG-1321
switch: creations.own_prebake
---

## Report
Key-frame review of the second 0.7.15 final check (out/frames-0.7.15-b.md, finding 5): S/050 of lab 20261117-154911 shows the guest's own creature plain green 2.2 s after a change; probably the game's bake not done yet. A candidate: not checked whether it lasts or happens without co-op.

## Root cause
Inferred (static, no game run): SetSpecies of a creation this game has not baked yet starts the game's own bake (IBakeManager);
until it ends the creature is drawn in its plain base coat. On a world load the swap waits 3 s (BUG-0318) with no bake
asked; a change of the own creature in a running world (the co-op window) swaps at once. A lane's graphics cache keeps a
bake once made (docs/re/creations.md#creations-model-bake), so only a key no game has baked shows it.

## Fix
creations.cpp apply_own_avatar (`coop.fix creations.own_prebake`, off = 0.7.16.1, the state still read for the tests): as soon
as the own key is wanted and resolves here (key_known + species_by_key, review #4) and the avatar has lived 1 s, the bake
is asked outside the frame every 0.5 s until it is baked; the swap waits for bake state 1 at most 5 s after the key was
wanted (captain::swap_waits). `coop.creations status` "own prebake: ... swap_bake=" is the game's IsBaked when SetSpecies
ran. Idea from 4e7490e6 (fix/agy-open-bugs), with the review's guard and retry.

## Verification
open, waiting for the lab run (no game run on this branch by day). Built, `make test` passes (tests/core_tests.cpp captain_rules). pending night run: own_prebake (PASS: a fresh copy no game has
baked, chosen in a running Creature world: swap_bake=1; key frame of the painted creature) and own_prebake_off
(SPORE_FIX_OFF=creations.own_prebake, must FAIL: swap_bake 0 or -2). Regressions near it: own_creature, creature_brain_hp,
editor_after_swap, npc_swap.

## History
- 2026-11-18: opened from the key-frame review (candidate); next release.
- 2026-11-23: feat/w74-captain (WP-K1): fix with a bounded wait for the bake, unit tests, lab test and twin written.
