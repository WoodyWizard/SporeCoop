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
commits: eb7a43d0
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
Lab 20261120-133703 (second final check, rel/0.7.16 673f7a55, lane 6) npc_air_under FAIL on ONE check, "the host's NPC
#196 was under its ground (-0.00 m)"; its other checks passed (the guest drew the proxy on its ground: median 0.00 m, 0 %
of the rows over 0.25 m under). The twin (lab 20261120-130608 npc_air_under_off, lane 4) FAILED as wanted: the guest
drew the proxy at -0.80 m in 100 % of the rows (monitor: 1 NPC proxy under the ground), with the same host check also
reading 0.00 m. So the host DID send its NPC 0.8 m under its ground (the guest without the fix air-placed it there), but
the host's own track rows cannot show it: they are taken at the start of the mod's frame, where the host's game had
already put the NPC back onto its terrain in its update (`coop.npc lift` runs after the row and before the send). A test
bug (the check measured the wrong thing), not the fix: the check now reads what the host sent, as the guest received it
(`coop.npctrack dump` column sa, the agl trailer; NF_AIR in the flags): at least 30 rows of the phase, 80 % NF_AIR,
median under LIFT + 0.3 m. Nothing was loosened: the guest's drawn-height checks are unchanged, and the twin still has
the same failing guest check.
Earlier: waiting for the lab run (no game today, owner rule). Tonight: npc_air_under PASS, npc_air_under_off
(`SPORE_FIX_OFF=npc.air_under`) FAIL, npc_air (the 4 m lift, unchanged) PASS, migration_heal PASS. Open, waiting for
the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): npc_air_under (the corrected host check) PASS in 20261121-003202, 20261122-194210, 20261123-023643; the twin npc_air_under_off FAIL in 20261121-000724 (and 20261120-130608): the guest drew 1 NPC proxy under the ground; migration_heal and nest_follow PASS in the same runs. Closed.

## History
- 2026-11-20: opened from the 0.7.16 final check; fixed on fix/w60-misc (npc.air_under), not run yet.
- 2026-11-20 (fix/w61-creature-c, second final check 20261120-133703): npc_air_under failed only its host-side
  precondition: the host's drawn rows are re-grounded by its game before it draws (0.00 m) while the state it sent was
  -0.80 m (the twin's guest drew -0.80 m). The precondition reads the received state now (npctrack column sa). Open,
  waiting for the lab run: npc_air_under PASS, npc_air_under_off FAIL.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
