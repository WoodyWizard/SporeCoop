---
id: BUG-9279
title: Lineages port phase 6 for Space: every player's own empire (own money and diplomacy)
status: open
kind: feature
stage: Space
reported: 2026-11-20
source: owner (Separated Lives)
role: 
versions: 
fixed_in:
branch: feat/lineages-space
commits:
tests: tests/space_empires_tests.cpp
signature:
docs: docs/re/space.md#space-own-empires
related: BUG-9274, BUG-9260, BUG-9290
---

## Report
The owner's wish ("Separated Lives"): in Space every player has their own empire (own money, colonies, ship and
captain, cards, diplomacy with the NPC empires), the players' empires allied for good, the core together.

## Root cause
One save, one player empire: a guest plays the host's cEmpire, shares its money (shared.cpp) and its diplomacy
(space_diplomacy.cpp). docs/re/space.md#space-own-empires.

## Fix
space_empires.cpp (MSG_SPACE 100..104): an empire record per party seat with a species of its own, kept by the host
next to the save (.duoempires, .duoempdiplo); its own money (K_ASK / K_EMPIRE / K_REPORT, shared.cpp leaves it
alone, contracts pay it); its own diplomacy (space_diplomacy.cpp shares nothing for it, the host keeps its table across
a rejoin: K_DIPLO / K_DIPLO_SET); its empire named "<player>'s empire" on its game. Switches: `space.own_empires`,
`diplomacy.own_empires`, `space.own_empire_name`.

## Verification
no-fail-run: no Linux lab here; by hand on lane C (two games, Steam exe 3010a413, 2026-11-20): Civ team victory ->
Space, the guest's empire founded, its own money (with `space.own_empires off` on both its change moved the host's
money: FAIL; on: PASS), its own diplomacy (with `diplomacy.own_empires off` its war reached the host: FAIL; on:
PASS), a leave and rejoin gave back its money and table (log). Unit test tests/space_empires_tests.cpp.

## History
- 2026-11-20: opened.
