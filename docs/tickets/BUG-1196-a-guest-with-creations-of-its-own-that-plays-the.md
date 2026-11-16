---
id: BUG-1196
title: suspected: a guest with creations of its own that plays the world's species does not take the host's evolved creature in place (not reproduced: it does)
status: not-reproduced
kind: bug
stage: Cell, Creature
reported: 2026-11-16
source: final check 20261116-025154 (parts_transition, leave_avatar_cell)
role: guest
versions: 0.7.5..0.7.14.1
fixed_in:
branch: fix/w51-t2
commits:
tests: evolve_in_place_own
signature:
docs: docs/re/creations.md#creations-follow-with-creations, docs/06-testing.md#testing-creature-after-worlds-reset
related: BUG-0508, BUG-0345, BUG-1215, BUG-1170
---

## Report
Found while triaging the final check 20261116-025154 (parts_transition, leave_avatar_cell): after the host's evolution
the host's log says "evolved in place: Bob keeps its world", then 30 s later "Bob still plays <old species> after 30 s:
sending the world"; the guest then goes through the galaxy menu and loads the whole world (3 MB). The guest plays the
world's species (no own creature chosen, no worlds.ini record) but has creations of its own (the lab's creations-guest:
5), as most players do.

## Root cause
Suspected from the code (creations.cpp `on_bundle`, the follow of BUG-0508): a guest records the host's new creation to
follow only when `g_opts[k].empty()` (no creations of its own), a condition older than BUG-0345 (0.7.5), after which a
guest with creations plays the world's species in a world it never played its own one in.
Not reproduced: in the lab such a guest follows all the same. evolve_in_place_own (creature fixture, the guest with the
5 creations of creations-guest, none chosen, no record): the guest's avatar takes the host's evolved species in place,
the guest stays in the stage, the host sends nothing after its 30 s check, both with the condition dropped (a switch
`creations.follow_with_creations`, 20261116-090728) and with that switch off, i.e. the old code path
(20261116-092810: "SPORE_FIX_OFF: creations.follow_with_creations off", `evolve in place: kept 1, sent after all 0`,
own creature 065CBE70 -> 2924E67B). Inferred: `mine()`'s BUG-0345 branch answers the host's applied creation (the
`g_applied` entry the new bundle replaced) for such a guest, so `apply_own_avatar` swaps without `g_follow`.
The world after all seen in the final check comes from the host's first creature editor after Cell keeping its species
key (BUG-1215), with or without creations.

## Fix
None in the mod: the switch tried on the branch was reverted (no behaviour change). Kept: the test
`evolve_in_place_own` (`coop_evolve_in_place.py --case creature --guest-creations`) as coverage of the configuration
most players have (the other evolve_in_place cases run the guest without creations); no twin exists for it.

## Verification
no-fail-run: not a bug; the old path (switch off) passed: out/lab/20261116-092810 (lane 10, evolve_in_place_own with
the condition in place). PASS also out/lab/20261116-090728 (lane 10).

## History
- 2026-11-16: opened.
- 2026-11-16: a switch dropping the condition tried on fix/w51-t2; its off twin passed too: not a bug, the switch reverted (not-reproduced).
