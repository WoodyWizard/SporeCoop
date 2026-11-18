---
id: BUG-9161
title: Creature: after a mating the egg scene never ends; the editor opens only on Escape (the family wears an older species)
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual: MSI host, TITAN host + guest, 2026-11-12/13)
signature: cinematic B6A1DBE1 (CRG_LaidEgg) playing for minutes after a mating, game mode Creature
docs: docs/re/creature.md#creature-egg-scene
related: BUG-0177 BUG-9160 BUG-0517
switch: creature.egg_watch creature.family_species
---

## Report
The owner (2026-11-12, host, TITAN): the nestmate answering the mating call showed the ally face, not the mate's, and no hearts; after clicking it they mated, but the creature never laid the egg; the editor opened only on Escape. Reproduced on the MSI (alone in a session and with a guest) and on TITAN: CRG_LaidEgg plays for minutes.

## Root cause
Observed (runtime and disassembly, Steam 3010a413): the scene ends only through the creature game's key handler 0xE45760 (event type 1, key 0x1B Escape or 0x54 T, [event+0x14] == 0) -> 0xE37920, the only writer of the flag +0x371 (with a 500 ms timer at +0x350); the handler then posts 0x4471C60 at 0xE45E0F, the message the evolution request is built on (0xD42E50). The two other references to 0x4471C60 (0xE7FBC4, 0xE80BC0) add / remove listeners. Observed: the own creature wore the player's own species (creations.cpp apply_own_avatar / BUG-0517 key, 2920607A) while its nest family and its herd (+0xA4) kept an older one (2919A821); the mate offer (CreatureClickAction 9) went to the one nestmate of the own species. Inferred: the egg step waits for something of the older species; not found.

## Fix
creature.egg_watch (on): when CRG_LaidEgg starts in Creature the own creature goes into the cocoon (decision 2D) and 2.4 s later the mod presses Escape for the player (the game's own end of the scene: the editor opens; without the cocoon: after 12 s). creature.family_species (on): every 2 s the living members of the own nest herd wear the own creature's species (SetSpecies, their age kept); the herd's own species (+0xA4) stays (with it changed no mate was offered any more).

## Verification
FAIL (2026-11-13): the MSI host alone and with a guest, the TITAN host: the scene played 70 s and more, no editor. PASS: TITAN host with a guest: the mating (Backspace, the mate clicked), the cocoon, the editor 2.4 s later (no vote), 0 faults. no-fail-run for the lab: manual runs.

## History
- 2026-11-13: opened.
