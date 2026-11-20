---
id: BUG-1357
title: Creature: a guest shows a host NPC proxy under its ground while the host's NPC is under the host's ground (NF_AIR with a negative height)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-20
source: final-check-20261120-041304
role: 
versions: 0.7.15.5, unreleased
fixed_in: unreleased
branch: fix/w60-misc
commits:
tests: npc_air_under, npc_air_under_off, migration_heal, nest_follow
signature:
docs: docs/re/creature.md#creature-far-npc-under-ground
related: BUG-1208, BUG-0550, BUG-0752, BUG-0399
---

## Report
Final check of 0.7.16 (lab 20261120-041304, lane 2): migration_heal passed all its checks and FAILED on the lab gate
monitor: "proxies: guest: 1 NPC proxies under the ground (Creature) on 2 lines from s 170". The same gate, species
and spot failed nest_follow in the 0.7.15 final check (BUG-1208, filed as an unproven flake).

## Root cause
Observed (migration_heal.guest.game.log): proxy 56 (species 065662CF) "0.79 m under this game's ground at (137.7
-480.2 -79.3) for 1.0 s, the host's NPC 0.0 m from it (driven)" at s 162, still at s 170 (9.1 s), then at s 174.6
"put onto it (npc.proxy_ground)". The proxy is ~70 m from the guest, outside the 60 m the host simulates ("npcs
simulated: 2 within 60 m"); both terrains agree there, so the host's own NPC stood 0.79 m in the host's terrain.
Observed (code): the host sends NF_AIR for any body that is not grounded and more than 0.15 m off GetHeightAt
(drive::body_airborne uses fabs, so under the ground counts), with the negative height in the agl trailer; the guest's
npc_rules::place then returns Air and drive::air_place puts the proxy at that height over the guest's terrain, i.e.
under it, and none of the ground rules (npc.proxy_ground, BUG-0399 / BUG-0752) runs for an air-placed proxy. When
the host's NPC came out of the ground (s 174.6) its state was no longer NF_AIR and drive::step put the proxy up at
once (the log line). Inferred: why the host's game leaves an unsimulated far NPC in its terrain is not known.

## Fix
mods/coop/npc.cpp + npc_proxy_rules.h: a state with NF_AIR whose height is under the host's ground (agl < -0.25 m,
drive::kUnderTol) is not in the air (`PlaceIn::host_under`): the proxy stands or walks on this game's ground (drive::step
with ground / walk_place ground). Guest side only, so a 0.7.15 host is covered. Switch `coop.fix npc.air_under off`
(the 0.7.15 placing). Unit check in tests/npc_tests.cpp (place() with host_under). Test: coop_npc_air.py --lift -0.8
(the host's game holds the NPC 0.8 m under its ground, `coop.npc lift`): the guest must draw its proxy on its ground
(median within 0.3 m, at most 10 % of the rows more than 0.25 m under).

## Verification
Waiting for the lab run (no game today, owner rule). Tonight: npc_air_under PASS, npc_air_under_off
(`SPORE_FIX_OFF=npc.air_under`) FAIL, npc_air (the 4 m lift, unchanged) PASS, migration_heal PASS. Open, waiting for
the lab run.

## History
- 2026-11-20: opened from the 0.7.16 final check; fixed on fix/w60-misc (npc.air_under), not run yet.
