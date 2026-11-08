---
id: BUG-0689
title: creature_brain_hp: the test expects the guest to play its own creature, but since BUG-0345 a guest plays the world's species in a world it never played its own in
status: fixed
kind: test
stage: Creature
reported: 2026-11-07
source: fix/w46-p4-creature regression run 20261107-212113
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-g4-tests
commits: 4d6dc86b
tests: creature_brain_hp
signature:
docs: docs/06-testing.md#testing-g4-tests
related: BUG-0345, BUG-0375
---

## Report
creature_brain_hp FAIL "the guest plays its own creature (not the world's player species)" on lanes 4 and 5:
fix/w46-p4-creature out/lab/20261107-212113 and 20261107-215918, and the rel/0.7.11 base 2577101f
(out/lab/20261107-220638, fix/w46-p4-creature tmp worktree). The other checks pass (both games at brain level 1, the
max health rises by the same step, each game shows the other's). The guest log says "own=065CBE70 from this world's
record (worlds.ini)": since BUG-0345 / BUG-0638 a guest plays its own creation only in a world it played (or chose) it
in, so the test's precondition no longer holds; its last pass was 2026-11-01 (fix/w30-creature-hp), before those
changes. Likely the test must choose the guest's creation in this world first (`coop.creations choose creature <id>`)
before the swap it expects. Not a player-facing bug (inferred). Reproduce: `tools/lab.py run creature_brain_hp`.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lab out/lab/20261107-212113 and this branch's 20261108-050533 base logs): the guest's log says "own=065CBE70
from this world's record (worlds.ini)": since BUG-0345 (creations.guest_world_species) a guest plays its own creation
only in a world it chose or played it in, so in the fixture world it plays the world's species and the test's
precondition "the guest plays its own creature" never held. Not a mod bug: the test predates BUG-0345.

## Fix
Test only (tools/tests/coop_creature_brain_hp.py): the guest chooses its newest own creature first
(creations_state.choose_own, as coop_creations.py / coop_creations_rejoin.py do) and the test waits until it plays it
(coop.hp `own ... different`) and the host shows its puppet; the guest's choice and world record are put back at the
end (creations_state.restore). The check of BUG-0375 itself is unchanged; its twin `SPORE_FIX_OFF=creature.own_brain_hp`
still fails.

## Verification
FAIL (before, the test's precondition): out/lab/20261107-212113, 20261107-215918 (fix/w46-p4-creature), 20261107-220638.
PASS 5 of 5 on lane 3: out/lab/20261108-050533 (#1, #2), 20261108-051411 (#1-#3).
Twin: SPORE_FIX_OFF=creature.own_brain_hp FAILS "the guest's own max health rose by the same step in its game (16.0 ->
16.0, host +5.0)": out/lab/20261108-051147. So the test catches BUG-0375 again.

## History
- 2026-11-07: opened.
- 2026-11-08 (fix/w46-g4-tests): the guest chooses its own creature first (creations_state); 5 of 5 PASS, the BUG-0375 twin FAILS; fixed.
