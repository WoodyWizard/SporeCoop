---
id: BUG-0704
title: Civ: an NPC nation's vehicles counted as a second player of a joint siege (a guest and an NPC army captured 50% faster)
status: fixed
kind: bug
stage: Civilization
reported: 2026-11-07
source: K2a audit of BUG-0420 (0.7.11)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2a-state
commits: 6b49ee3d
tests: civ_siege_npc (coop_civ_siege_npc.py)
signature:
docs: docs/07-modes.md#modes-message-classes
related: BUG-0420
---

## Report
K2a audit of the class BUG-0420 (no player report): the host's joint-siege rule (capture damage x1.5 when two players attack a city within 10 s) counted the owner of every attacking vehicle; an NPC nation's vehicle has the owner 0xFE in the mod's table and was counted as a player, so one guest attacking a city that an NPC army attacked too captured it 50% faster and everybody was told "joint siege".

## Root cause
Observed: civ.cpp `player_of_combatant` returns the `g_hv` owner, 0xFE for NPC vehicles; `h_AddCapture` tested only `g_hit_by != 0xFF`. The game's capture meter (city +0x748) rose by 1.5 instead of 1.0 for the guest's hit after an NPC army's (lab 20261107-222158).

## Fix
`civ.siege_players`: only a player id (< net::kMaxPlayers) enters the siege table; an NPC hit is counted (`siege_npc=` in coop.status). `coop.fix civ.siege_players off`: 0.7.10.

## Verification
coop_civ_siege_npc.py (the game's capture meter through its own AddCapture, fixture civ_npc):
* FAIL with `SPORE_FIX_OFF=civ.siege_players`: out/lab/20261107-222158 (capture +1.5 for the guest's hit after the NPC army's).
* PASS: out/lab/20261107-222015, out/lab/20261107-223602 (+1.0; the host's hit after the guest's +1.5, the joint siege still works).

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state): fixed, civ.siege_players.
- 2026-11-08: released in 0.7.11.
