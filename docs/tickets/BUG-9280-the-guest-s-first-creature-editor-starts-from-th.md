---
id: BUG-9280
title: the guest's first Creature editor starts from the host's creature, not from the cell it chose (start choice)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-20
source: lane D integration QA (test/integration)
role: guest
versions: 0.7.15.94
fixed_in: unreleased
branch: fix/guest-landfall
commits: caf95ff8, 7db156c3
tests: tools/tests/coop_newgame_landfall.py (manual driver, Windows lane)
signature:
docs: docs/re/creations.md#creations-guest-landfall
related: BUG-9273, BUG-1255, BUG-0278
switch: creations.guest_landfall
---

## Report
Integration QA of the whole campaign (build/amigos 0.7.15.94 + feature branches): the guest chose its own cell in the
start choice (Carnivore, "Glorbo", blue body) and played it through the Cell stage. When both reached Creature, the
guest's own landfall editor opened with the HOST's creature (an orange herbivore with the host's name), not a creature
grown from the guest's cell. Done without an edit, the guest then played the host's species (both party seats the same
species).

## Root cause
Observed: vote.cpp open_own_editor starts the guest's editor from the avatar's species, and in the host's new Creature
world that is the world's (host's) species (BUG-1255 notes). The game's own landfall makes the land creature from the
player's cell with CellModelToEarlyCreature (0x4BA690) only for the local player of a single-player game; Lineages did
the same for a guest's own cell (its BUG-9006, landfall_tick / make_land_creature) and this was not ported.
On the first port (agent Q, uncommitted) the conversion ran (a new creature key minted, the BEM written) but the result
was never found: My Creations was not flushed (Lineages calls the database's vt +0x24 before it reads the record), so the
guest got "Your land creature could not be saved" after 30 s and still the host's creature.

## Fix
creations.cpp landfall_tick (ported from Lineages BUG-9006): while the guest plays its own cell in the host's Cell world
the cell's key is kept; in the Creature stage of the same planet, outside the frame, the cell's model is converted
(CellModelToEarlyCreature), given a new creature key (App::cIDGenerator), registered and cached, written to My
Creations with the cell's picture and a metadata of its own (the start choice's name when the cell has none), then My
Creations is flushed and the creature chosen as the guest's own. vote.cpp: the guest's landfall editor waits for it
(creations::landfall_pending). coop.creations prints the state (`landfall: fix= cell= world= result= made= fails=`).
Switch: `coop.fix creations.guest_landfall off` = the editor starts from the host's creature (0.7.15.94).

## Verification
Lane B (Windows, Steam build), the integration build (test/integration), a new Cell game each run, the guest picks
Carnivore "Glorbo" blue in the start choice, both fill their own bars, the vote moves both to Creature:
- FAIL without the fix: agent Q's run on 0.7.15.94 (lane D): the guest's landfall editor showed the host's "Glarg"
  herbivore. First port without the flush (lane B, planet Orley): `landfall: made=0 fails=1`, the editor showed the
  host's yellow herbivore "Gaal".
- PASS with the fix (lane B, planet Homan): `landfall: ... made=1 fails=0`, the guest's editor opens on a blue Carnivore
  "Glorbo" grown from its cell; after the editor the guest plays 7C1A600D 'Glorbo' at its own nest 56 m from the host's;
  the host's game shows the guest's creature as 'Glorbo' (coop.creations player 1) and the party's seat 1 has species
  7C1A600D (seat 0 the host's 292AC9EB).
- Driver: tools/tests/coop_newgame_landfall.py (the same steps in one script; --landfall-off / --party-off are the twins). The runs above were made with its steps as separate scripts; the combined script waits for a lane.
- no-fail-run: no lab test drives the start choice and the Cell stage end to end yet; FAIL and PASS were run by hand on
  lane B as above.

## History
- 2026-11-20: opened.
- 2026-11-20: fixed on fix/guest-landfall (from feat/start-choice): Lineages' landfall conversion ported, with the
  flush of My Creations the first port missed.
