---
id: BUG-0819
title: Creations: the BUG-0200 species alias also runs while the guest plays its own world (suspected)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-own-world
commits: dda0afe5, 37dc770a
tests: own_creature
signature:
docs: docs/re/creations.md#creations-alias-own-world, docs/re/host-centric.md#host-centric-own-world, docs/06-testing.md#testing-own-world
related: BUG-0200 BUG-0341 BUG-0452
---

## Report
No player report. In a connected guest's own save, NPC nests whose species instance collides with one of the host's own creations show the host's creature; suspected: herd members spawned meanwhile may carry the alias key into that save.

## Root cause
Observed at runtime: in a Creature world of its own the connected guest's species manager still answered the host's key 290229B9 with the host's creation (key 7D851520, bem 6E1A058F, aliased 1) instead of the guest's own creation of that id (bem 9B6134E2) (lab 20261112-031303). Not checked: whether a herd member spawned meanwhile writes our key into that save (the inferred BUG-0341 class); the fix removes the alias, so the question is moot while it holds.

## Fix
alias_tick undoes every alias (the slot gets its profile back; null: the game builds the guest's own on the next lookup) while own_world::elsewhere() or own_world::space_other(), and sets them again when the guest plays the host's world. `coop.creations speciesof` shows `own_world <n>`. Switch `coop.fix creations.alias_own_world off`.

## Fix idea
Run the alias only while the guest plays the host's world; undo it when the guest goes elsewhere, as at the session end.

## How to test
nest_species fixture (host creation 290229B9) and a guest save with a species of the same instance; the connected guest loads its own save: `coop.creations speciesof 290229B9` must stay the guest's own key (fix) vs 7D... (main); save, restart solo, check the herd's +0xB28.

## Verification
Test `own_creature` steps 1 and 3: the guest with colliding My Creations (coop_nest_species.restart_guest), the alias set in the host's world, then the guest's own world.
* FAIL: out/lab/20261112-031303 (`SPORE_FIX_OFF=creations.alias_own_world`, lane 5): key 7D851520 bem 6E1A058F aliased 1 in its own world.
* PASS: out/lab/20261112-030351, out/lab/20261112-030546: key 290229B9 bem 9B6134E2 aliased 0 (restored 1).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-own-world, O1 of 0.7.13): fixed with creations.alias_own_world; FAIL 20261112-031303, PASS 20261112-030351 / 030546.
- 2026-11-12: released in 0.7.13.
