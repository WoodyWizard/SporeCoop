---
id: BUG-0939
title: Tribe: right after Creature -> Tribe the arrow of a guest that has no chief yet points to the planet's centre ('Dave 504 m')
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-090547
role: host
versions: 0.7.12 (unreleased)
fixed_in: 0.7.12
branch: fix/w47-arrow-origin
commits: 5a8f6015
tests: find_partners_tribe, find_partners, find_partners_cell, find_partners_civ, find_partners_space
signature:
docs: docs/08-ui.md#ui-find-each-other
related: BUG-0274, BUG-0339, BUG-0907
---

## Report
Key-frame review of the second final check out/lab/20261111-090547 (rel/0.7.12 94ca59ab).
Sheet: `out/lab/20261111-090547/frames/sheets/005_campaign_04_advance-creature.jpg`. Frames: `out/lab/20261111-090547/frames/campaign/04_advance-creature_host-Alice.jpg`,
`04_advance-creature_guest-Dave.jpg` (campaign, 4 players, the frame right after the Creature -> Tribe change).

* Host: at the bottom edge an arrow in Dave's colour with "Dave 504 m", pointing down. The village shows three
  chiefs ("Alice's chief", "Carol's chief", "Bob's chief"); there is no chief of Dave.
* Dave's screen: the same village and three chiefs, none of its own; its roster has two cards.
* The manifest has Dave's `coop.avatar` position as (0.0 0.0 0.0); the host is at (129.3 -471.8 -119.8), 504 m
  from the planet's centre: the arrow points at the centre of the planet.
* At the end of the Tribe play (sheet 006) Dave has a position and a chief, so the state passes.

Severity: low (a wrong arrow for some seconds after a stage change). Confidence: high for the arrow and its
distance; how long it lasts is not known (one frame). Regression of 0.7.12: unknown (the arrows are from 0.7.10,
find_partners).

## Root cause
Confirmed (reproduced, lab 20261111-110944). `find_targets` (mods/coop/coop.cpp) picks a partner's position in Tribe
as its chief, else its puppet. The Tribe / Civ "puppet" is the partner's pointer marker (modes.cpp `rts_fill`): the
planet point under its mouse. A game whose mouse never moved over the planet (a guest right after the stage change,
every lab game) reports the marker's initial position (0 0 0) with PF_IN_WORLD set, so the host makes the marker
puppet at the planet's centre. A guest in the stage without a chief yet therefore became a target at (0 0 0): an
arrow with the host's own distance from the centre ("504 m"), and a minimap blip at the same point. The only check
was "finite". The same source gave Civ's name tag of the pointer and the own "me" position (the distance text).
The chief and capital sources did not ask whether the partner is in this world and stage at all (a partner in an
editor or loading kept its arrow to its chief).

## Fix
Switch `find.placed` (`coop.fix find.placed off` = the 0.7.11 behaviour). A partner is a target of the arrows, the
minimap and the name at its puppet only when
* it is shown here: its puppet exists, i.e. the existing notion of a valid state (drive_puppet: a fresh state with
  PF_IN_WORLD, not dead, of this mode and world), for every source (chief, puppet, capital) and every stage, and
* its position is a place (`findx::placed`, find_core.h, unit-tested in tests/validate_tests.cpp): finite, not the
  zero vector, and in Creature, Tribe and Civ farther from the planet's centre than half of the own avatar's
  distance from it (no fixed distance; Cell's plane and Space have no such centre: the first two rules).
Tribe: the chief, else the pointer when it is a place; Civ: the pointer when it is a place, else the capital. The
own position follows the same rule (no distance text from an own pointer at 0 0 0). Not changed: the host still
notes a view centre at a partner's unplaced pointer (views::note, harmless: nothing lives at the centre).

## Verification
`find_partners_tribe` (tools/tests/coop_find_partners.py `unplaced`): control (the host has the guest's chief as a
row), then the guest's pointer is pinned at 0 0 0 (`coop.npc marker 0 0 0`, the state of a pointer never put on the
planet) and its chief leaves the tribe (`coop.tribe drop_chief 1`); the guest's game reports the position 0 0 0;
for 3 s the host's game must have no arrow / tag row (`coop.find`), no name label (`coop.find labels`) and no blip
on the game's own minimap (`coop.find map`) for the guest; then the guest's pointer is pinned 30 m from the host's
chief and the host must have the row and the blip there.
* FAIL with `SPORE_FIX_OFF=find.placed`: lab 20261111-110944, 20261111-111249 (host: `arrow pos=0.00,0.00,0.00
  dist=525.0 drawn=1`, blip at 0 0 0).
* PASS: lab 20261111-111141 (no row, no blip, no label; the pointer back: arrow at the pointer, 31.4 m), and
  20261111-111412 (find_partners, find_partners_tribe, find_partners_cell, find_partners_civ, find_partners_space:
  5/5).
* By eye: `frames/find_partners_tribe/01_tribe_unplaced_host.jpg` of lab 20261111-111249 (off) shows "Bob 525 m"
  with an arrow at the bottom edge; the same frame of lab 20261111-111141 has no arrow and no name of Bob.
* `make test` (the unit check of `findx::placed`).

## History
- 2026-11-11: opened.
- 2026-11-11: fixed on fix/w47-arrow-origin (find.placed).
- 2026-11-11: released in 0.7.12.
