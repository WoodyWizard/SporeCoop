---
id: BUG-1198
title: world_reload, lend_ledger, editor_after_swap / editor_after_load relied on the guest's own-creature record a lane's history left in worlds.ini (BUG-1176 resets it at the cold start)
status: open
kind: test
stage: Creature
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Final check 20261116-025154 (rel/0.7.15 11114769): world_reload FAIL "before: the guest plays its own creature" (and both
rounds), lend_ledger FAIL "the guest plays a creature of another species than the world's player species". Both
reproduced on lane 10 (fix/w51-t2 before any change, run 20261116-082820 for the others of the group).

## Root cause
Observed (logs of 20261116-025154): the guest's game says `own=065CBE70 '' from the world's species (no own one played in
this world)`; `coop.creations` "mine creature" is the host's species. Since BUG-1176 (feat/w51-labstate) the cold start
removes the guest's worlds.ini, so the guest plays the fixture world's species unless the test chooses its own
creation (BUG-0345). Both tests were written when the lane's leftover record made the guest play its own creature.
editor_after_swap / editor_after_load had the same dependency the other way round: `--no-swap` stepped with
`coop.creations prev` from the world's species onto an own creation (and recorded it for the world), and the swap case
played the world's species (no swap after the rejoin).
An intended change of 0.7.15 (BUG-1176), not a mod bug.

## Fix
* coop_world_reload.py, coop_lend_ledger.py: `creations_state.save` / `choose_own` at the start (the newest creation of
  creations-guest), `restore` at the end; lend_ledger waits up to 30 s for the swap (`coop.hp species` same=0).
* coop_editor_after_swap.py: the swap case chooses the own creature, `--no-swap` keeps the world's species (checked: the
  guest's "mine creature" is the host's avatar species) instead of stepping off it; the profile is restored at the end.
Nothing the tests check was loosened.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
