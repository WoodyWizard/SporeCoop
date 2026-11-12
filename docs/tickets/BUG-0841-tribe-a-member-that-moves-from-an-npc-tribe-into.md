---
id: BUG-0841
title: Tribe: a member that moves from an NPC tribe into the player tribe keeps owner 0xFE and nobody can select it (suspected)
status: not-reproduced
kind: bug
stage: Tribe
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch: fix/w48-tribe-civ
commits:
tests: tribe_member_moves
signature:
docs: docs/re/tribe.md#tribe-member-moves
related:
---

## Report
Suspected from the code: a tribe member that joins the players' tribe from a village stays unselectable for every player, the host included.

## Root cause
Not reproduced: the game never moves a member from one tribe to another.
* Observed (Ghidra, GOG 2024): `cTribe::AddMember` 0xC95F00 (returns at once for a citizen already in the list) has
  three callers: SpawnMember 0xC983C0 (a new citizen), the thunk 0xC97010 (AddMember(c, 1)) and the population set-up
  0xC9A290 (vt 0x8C: new citizens made with CreateInstance(0x18EB4B7) up to the tribe's count). The thunk's callers
  are 0xACA2A0 (the noun manager's placement of a citizen made outside Tribe mode: `mode != 0x1654C02`), 0xAD11E0 and
  0xAD2260 (new citizens of a load / a hut's spawn). `RemoveMember` vt 0x9C 0xC96070 with destroy = 0 only detaches
  (SetTribe(0)); nothing re-adds a detached citizen to another tribe. A conquest (cTribe_SetConquered) and an
  alliance (TribeToTribeMission_Complete kind 2) move nobody (docs/re/tribe.md#tribe-conquest-sync).
* Observed (runtime, lab 20261112-084851 and 20261112-091004, `tribe_member_moves`): the host's table counted no
  member changing its tribe (`member_moves=0/0`) through an alliance (a village's relation to the player tribe set to
  10, the allied level) and a conquest (the village's +0x556 set); the player tribe's members stayed the same three,
  each with a player as its owner.
So a member of the player tribe with owner 0xFE cannot come from a move; the host's split gives every new member of
the player tribe an owner.

## Fix idea
When a member's tribe changes to the player tribe, hand it an owner in turn as for a new member.

## How to test
Conquer or ally a village in Tribe and check `coop.tribe` member list for player-tribe members with owner 254; try to select one on the host: FAIL = owner 254, not selectable; PASS = owned by a player.

## Fix
The host's table now counts members that change tribes (`member_moves=<all>/<into the player tribe unowned>` in `coop.tribe`), for tests and support logs; no behaviour change.

## Verification
no-fail-run: not reproducible (the game adds only new citizens to a tribe). Evidence: `coop_tribe_member_moves.py`
(suite `tribe_member_moves`, tribe fixture, ~2.5 min): an alliance and a conquest in the host's game, the player
tribe's members and owners and the host's count of members that changed tribes (`coop.tribe`: `member_moves=`):
lab 20261112-084851, 20261112-091004 (0/0, every member owned).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12 (fix/w48-tribe-civ): not reproduced: the game never moves a member between tribes (Ghidra: the callers of AddMember; lab: an alliance and a conquest).
