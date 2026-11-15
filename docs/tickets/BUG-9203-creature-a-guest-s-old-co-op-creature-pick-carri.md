---
id: BUG-9203
title: Creature: a guest's old co-op creature pick carries into a brand-new world, so the guest never gets the creature editor
status: fixed
kind: bug
stage: Creature
reported: 2026-11-14
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: a8a3ceca
tests: tools/qa_win (TITAN, host 9 + guest 8 over 127.0.0.1, 2026-11-14)
signature:
docs:
related: BUG-9164 BUG-0344 BUG-0345 BUG-9159 BUG-9170
---

## Report
Owner play test 2026-11-14 (3 players, the host started a new Creature world from the galaxy menu with two guests waiting): the guests were not put into the creature editor although they had no creature on that world; each played a creature from its own collection.

## Root cause
Observed (host and guest logs): each guest used its co-op window pick saved in choices.ini (guest log: `own=... 'Duckulon' from choices.ini`; host: "plays its own creature"). Inferred from code: BUG-0345 makes a co-op window pick hold everywhere (`creations::mine()`, creations.cpp around 1468-1500); the host already drops a pick made in another world (BUG-0344, `!guest`), guests do not. BUG-9164's `join_editor_tick` (coop.cpp around 2466-2502, switch creature.join_editor_first) skips a guest whose `plays_own_creature()` is true, so the old pick blocks the editor. Side note: during the host's pre-game editor the guest status line says the host "is evolving the species in the editor" (coop.cpp around 6686), which is wrong for a new world.

## Fix
creations.cpp: worlds where this guest's own creature was first played in this run (its worlds.ini record was new) are noted; coop.cpp join_editor_tick (BUG-9164) gives such a guest its own editor too, starting from its pick (decision 3T A). The guest's status line during the host's pre-game editor now says the host is making a new world, not evolving. Switch: `coop.fix creature.new_world_editor off`.

## Verification
no-fail-run: manual on TITAN (no Windows lab lane). FAIL (19:35, the build before): the guest's pick 'Duckulon' was recorded new for world FEC6C134 and no `join: own editor` line followed in 90 s. PASS (19:39, a8a3ceca): the same world made new again (its worlds.ini record removed), `join: own editor for a creature of our own` 12 s after the guest appeared and the editor opened 22 s later (held for the new body), screenshot of the guest's editor.

## History
- 2026-11-14: opened.
- 2026-11-14 (night): fixed (decision 3T A), live on TITAN.
