---
id: BUG-0406
title: Creature: the host's animals walking downhill keep starting to fall on the guest's screen (walked proxies placed exactly on the ground)
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: lab measurement (fix/w33-proxy-ground, npc_walk_slope), lane 7
role: guest
versions: unreleased
fixed_in:
branch:
commits:
tests: npc_walk_slope (tools/tests/coop_npc_walk.py --goto=-195,-465,-45 --falls 3)
signature: walked proxies' +0x137 goes 1 -> 0 (fall) while the host's NPC walks downhill
docs: docs/re/creature.md#creature-npc-ground
related: BUG-0366, BUG-0361, BUG-0399
switch: npc.walk_ground
---

## Report
Inferred by the BUG-0366 work (docs/re/creature.md#creature-puppet-ground: "NPC proxies placed every frame
probably fall downhill the same way"), measured here: the host's game walks 4 NPCs around the guest's
avatar on a slope of the creature fixture (`coop_npc_walk.py --goto=-195,-465,-45`, coop.npctrack every
frame on both games). The guest's walked proxies started to fall (+0x137 1 -> 0, the fall animation)
51.1 and 22.7 times per 100 s of walking, the host's own NPCs 0.9 and 0.8 (out/lab/20261101-142745
npc_walk_slope#1, #2). A guest sees the host's animals hop while they walk downhill.

## Root cause
Observed (the counts above), mechanism as BUG-0366 for the partner's puppet (observed there): npc.walk
(BUG-0361) places the proxy exactly on the host's path every frame; walking downhill it is a few cm over the
ground after the game's next step and the game takes it as falling.

## Fix
`npc.walk_ground` (npc.cpp): the Creature proxies' walk_place gets `WalkPlace::ground` (driver.h
on_own_ground: a target from 3 m under to 0.5 m over this game's ground goes 5 cm into it), as the puppet's
creature.puppet_ground. `coop.fix npc.walk_ground off`: on the host's path exactly (0.7.3.4).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
