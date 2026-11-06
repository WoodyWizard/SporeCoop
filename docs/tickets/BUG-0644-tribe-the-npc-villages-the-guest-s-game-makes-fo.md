---
id: BUG-0644
title: Tribe: the NPC villages the guest's game makes for the host's tribes have other colours than on the host
status: open
kind: desync
stage: Tribe
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-13, Sterben-07
role: both
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0431
---

## Report
Discord report 2026-11-06 (0.7.8): the guest (discord-2026-11-06/M0ng0r-13, s 4206) "village color is desynced"; the host
(Sterben-07, s 4209) "other villages have different color for guest and host".

## Root cause
Observed (the guest's log): "the host's NPC tribe N ... is not in this game: made it (species 29158397, archetype X)" for
tribe 1 at 18:38:54, tribes 2-4 at 18:44:15-18, tribe 5 at 18:56:17.
Observed (code): tribe.cpp make_npc_tribes builds them with `SpawnNpcTribe(pos, archetype, 0, 0, 0, species)`: only the
archetype and the species are copied; no colour (ID colour) is sent or set.
Inferred: the guest's game picks its own colour for each tribe it makes. Class BUG-0431 (requests the mod builds
incompletely). The colour field's offset is not known yet (RE needed).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: play_tribe until the host's game forms NPC tribes the guest makes; compare each tribe's colour on both games
     (find the field: cTribe and its political id colour, docs/re/tribe.md). -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
