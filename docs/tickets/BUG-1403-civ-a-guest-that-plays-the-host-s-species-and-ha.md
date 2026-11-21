---
id: BUG-1403
title: Civ: a guest that plays the host's species and has own creatures cannot pick a civilization role (the creature row holds the arrows)
status: open
kind: bug
stage: Civ
reported: 2026-11-21
source: lab 20261121-003202 play_civ (found on the way)
role: guest
versions: rel/0.7.16 21025f40
fixed_in:
branch:
commits:
tests: play_civ, late_species
signature:
docs:
related: BUG-1289, BUG-1402
---

## Report
Found on the way (BUG-1402, lab 20261121-003202 play_civ): in Civ, a guest that plays the host's species and has own
creatures in My Creations sees "My creature: the host's species (in My Creations: 5) < >" in the co-op window's row,
where 0.7.15 showed "My role: none - pick one < >". The arrows pick a creature (BUG-1289, by design), so this guest cannot
pick a civilization role (military / economic / religious) unless it first picks a creature: an own one, or the host's
species again with the arrows, and then waits 30 s (creations::late_row keeps the row 30 s after a click; late_pick is
false only after an explicit choice). A player who wants to keep the host's species and take a role has no visible way.

## Root cause
Observed in the code: coop_ui.cpp refresh_lobby: `roles` needs `giving`, and `giving` needs an empty creature row
(`cl.empty()`); a.creation (coop.cpp) returns the row in Civ while creations::late_row is true. Not a test or flake
cause by itself; play_civ's guest role actions do not run while it holds (BUG-1402). New in 0.7.16 (c29e3ba4).

## Fix
Not fixed (outside the flake task; a UI decision): for example the role on the row's second line, or the creature
row only until the guest's first choice in this world, or a role button. Needs the owner's / the Civ UI package's call.

## Verification
Open. A test would be: guest in Civ with own creatures, playing the host's species, picks a role through the window
(coop_late_species already shows the row; it asserts the arrows do not change the role).

## History
- 2026-11-21: opened.
