---
id: BUG-1208
title: Final check 0.7.15 flakes: nest_follow (a far NPC proxy under the guest's ground), npc_walk_player (walk gait 78%), creature_kills (the hunted NPC fled), sreport_leave (monitor drawn, see BUG-1194), tag_height (one shot)
status: not-reproduced
kind: test
stage: Creature
reported: 2026-11-16
source: lab
role: 
versions: 
fixed_in:
branch: fix/w51-t5
commits:
tests: nest_follow, npc_walk_player, creature_kills, sreport_leave, tag_height
signature:
docs:
related: BUG-1194, BUG-1200
---

## Report
Final check of 0.7.15 (lab 20261116-025154, rel/0.7.15 11114769): these tests of the Creature / NPC / Cell group
failed once and passed twice in a row on the same code in the reruns on fix/w51-t5 (lab 20261116-082817, lanes 2-3,
before any change of these tests). `tools/lab.py flaky` history before the final check:
* nest_follow: gate monitor `proxies: guest: 1 NPC proxies under the ground (Creature) on 2 lines from s 170`;
  history 4 of 24 failed on all branches since 2026-11-07 (1 of 4 on rel/main, other checks); reruns PASS, PASS.
* npc_walk_player: `walk gait in 78% >= 80% of 3091 moving frames`; history 3 of 13; reruns PASS (89 %), PASS (86 %).
* creature_kills: the hunted NPC #67 fled to 200 m in 90 s (9 combat keys), not killed; history 8 of 20 on rel/main,
  17 of 84 on all branches (BUG-0690's flight); reruns PASS, PASS.
* sreport_leave: gate monitor `drawn ... 3.9 m` (a monitor false match, fixed in BUG-1194); history 1 of 11; reruns
  PASS, PASS.
* tag_height: the tag not found in the still shots (the game's rollover of an NPC over it); history 2 of 13; reruns
  FAIL (0.74 m > 0.7: the partner 8.5 m away among the nest's red creatures), PASS; v0.7.14.1 PASS
  (lab fix/w51-t5-v07141 20261116-084839). Its geometry is restored by BUG-1200.

## Root cause
* nest_follow (inferred, not proven): proxy 57 (species 065662CF) was driven 0.0 m from the host's NPC and 0.69 m under
  the guest's ground at (149.1 -477.2 -72.6), ~110 m from both players (outside the 60 m of simulated NPCs) while the
  guest sat in its nest-claim cinematic; the host's NPC itself stood there. Driven proxies under the guest's ground
  were seen in many versions (0.7.3-0.7.9 runs, a far NPC on terrain of another detail); the same proxy spot showed
  once on feat/w51-harness (20261115-234226) without failing. Not a 0.7.15 regression by the evidence at hand.
* npc_walk_player: the proxies' walk gait on the guest's screen at 30 fps over the player network varies run to run
  (78-89 %), the host's own NPCs 99 %; within the known spread of BUG-0394's test.
* creature_kills: the NPC's flight outran the guest's hunt (BUG-0690 / BUG-0944 notes); a game AI outcome.
* sreport_leave: BUG-1194. tag_height: BUG-1200.

## Fix
No mod change. sreport_leave and tag_height are covered by BUG-1194 and BUG-1200; the other three stay as flakes with
their numbers recorded here. switch: none (no change).

## Verification
no-fail-run: flakes, recorded with numbers; the failing run is the final check 20261116-025154, the passing reruns
are lab 20261116-082817 (nest_follow#1/#2, npc_walk_player#1/#2, creature_kills#1/#2, sreport_leave#1/#2) and the
group's final runs on fix/w51-t5 (lab 20261116-085451: all PASS twice).

## History
- 2026-11-16: opened.
- 2026-11-16: closed as flakes (fix/w51-t5): PASS twice in reruns 20261116-082817 and again in 20261116-085451.
- 2026-11-20: nest_follow's proxy under the guest's ground: the cause is BUG-1357 (the host's far NPC 0.7-0.8 m under its own ground, sent as NF_AIR with a negative height, air-placed under the guest's ground); the same spot and species failed migration_heal in the 0.7.16 final check 20261120-041304. Fixed there (npc.air_under).
