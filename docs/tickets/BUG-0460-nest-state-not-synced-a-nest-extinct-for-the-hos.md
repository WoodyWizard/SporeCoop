---
id: BUG-0460
title: nest state not synced: a nest extinct for the host is not extinct for the guest (minimap, home nest heal)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
versions: 
fixed_in: 0.7.4
branch: fix/w40-world-objects
commits: 9445045a 5ec41dc0 ffed3f29 0209a1c2
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
`coop_world_objects.py --kind herd` (suite wobj_herd): the host's avatar kills the creatures of the nearest nest of
another species; the herd's extinct bits in the guest's game.
* FAIL with `SPORE_FIX_OFF=wobj.herd`: out/lab/20261103-132808 (host +0x190 = 1, guest 0).
* PASS: out/lab/20261103-132718.
* The home nest heal is BUG-0496 (not analysed). The minimap itself was not looked at (~ nest +0x120 is inferred
  to be what it shows).

## History
- 2026-11-02: opened.
- 2026-11-03: fix/w40-world-objects: fixed (worldobj.cpp / nest.cpp).
- 2026-11-03: seen again in a player report 2026-11-03 (0.7.3.6, host mark 'map desynced on extinct nests (dead nests)', report 20261103-192547), fixed in 0.7.4 (unreleased).
- 2026-11-03: released in 0.7.4.
