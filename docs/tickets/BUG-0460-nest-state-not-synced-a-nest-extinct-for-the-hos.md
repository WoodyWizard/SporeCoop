---
id: BUG-0460
title: nest state not synced: a nest extinct for the host is not extinct for the guest (minimap, home nest heal)
status: open
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
versions: 
fixed_in:
branch: fix/w40-world-objects
commits:
tests: wobj_herd (tools/tests/coop_world_objects.py --kind herd)
signature:
docs: docs/re/creature.md#creature-world-objects, docs/06-testing.md#testing-world-objects
related:
switch: wobj.herd
---

## Report
Guest's marks: s 1637 'i don't see nests extinct on minimap', s 115 (later session) 'for me nest is not extinct and i don't get any heals from it'. Nest extinction / home nest heal state is per game. Class G4 (BUG-0421).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (Ghidra): a species' extinction runs 0xBA4750 from cSpeciesRelationshipManager::ApplyEvent: cHerd_SetExtinct
0xC6AED0 on every herd of the species (+0x190, nest +0x120). The guest writes the host's relationship table
(social.cpp, MSG_SPECIES) but never runs it, and its herds have no members (the host's NPCs are proxies): its
nests stayed alive. Runtime (lane 9): a herd whose members the host's avatar killed had +0x190 = 1 and its nest
+0x120 = 0 on the host, 0 / 0 on the guest. The home nest heal of the report was not analysed.

## Fix
worldobj.cpp kind `herd` (Creature): the host's herd +0x190 / nest +0x120 is applied to the guest's herd of the
same home with the game's setters (cHerd_SetExtinct, cHerd_SetExtinction 0xC70160). Switch `wobj.herd`.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
