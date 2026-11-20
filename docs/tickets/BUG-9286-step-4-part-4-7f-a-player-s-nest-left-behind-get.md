---
id: BUG-9286
title: Step 4 part 4 (7F): a player's nest left behind gets a new species from the game's own pick
status: fixed
kind: feature
stage: creature
reported: 2026-11-20
source: 
role: 
versions: 
fixed_in: unreleased
branch:
commits: 33c6a036
tests: tools/tests/galaxy_server_check.py (server side); in game: TITAN QA runs (docs, Verification)
signature:
docs:
related:
---

## Report
Step 4 part 4, second half (decision 7F). A party that moves on leaves its nests on the old planet as wild animals
(6Z); a player away keeps a nest too. On a persistent planet those must rotate out. Owner's answer to 7F: the place
gets a species the way Spore normally picks one, the Sporepedia cache included.

## Root cause
Feature. The game picks every wild nest's species through a slot of the planet's pick table (PlanetPickSpecies
0xB9A8A0); a player's nest has no slot (herd +0x8C 0), which tells the two apart. docs/re/creature.md#creature-species-pick.

## Fix
The galaxy server's `--rotate-hours` (72 by default) goes to every player in WELCOME. The planet's host notes when no
player of a player-nest species is on the planet (`<save>.spo.duo-left`, the server's clock); after the rotation time
every nest of it gets a species from the game's own pick (no slot, the nearest wild nest's archetype and generation),
written as the game's evolution step writes it, with a slot of its own (0x7E000000 | instance), no best friends, no
evolution of its own. Guests follow through the herd_evo row (a species without a chain by key, an unknown one asked
from the host, +0x191 carried). Switches `creature.nest_rotate` (host), `wsync.nest_rotate` (guest).

## Verification
TITAN QA runs (no lab on Windows), docs/12-server-and-players.md#server-nest-rotation-built:
- FAIL (switch off, clock +73 h): 0 of 25 left nests changed. PASS (on): 25 nests of 14 species got new species, their
  animals turned within a minute; images/stage4_nest_rotation_new_species.png.
- Guest FAIL (the build before wsync.nest_rotate): 13 of 126 guest nests kept the old species. PASS: 0 of 125 differ.
- A player leaving: noted at once, rotated after the (test) 72 h. Save, upload, reload: 26 of 26 kept.
- tools/tests/galaxy_server_check.py: WELCOME carries the rotation (72 h).

## History
- 2026-11-20: opened.
- 2026-11-20: built and seen in game (TITAN, two QA games).
