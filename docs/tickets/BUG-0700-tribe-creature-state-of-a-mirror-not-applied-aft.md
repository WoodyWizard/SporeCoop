---
id: BUG-0700
title: Tribe / Creature: state of a mirror not applied after its spawn: a member's age, carried food; an NPC's befriended and stun state
status: fixed
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0419 (0.7.11)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state-b
commits: 1391b0ac 149ac2b6
tests: creature_stun_proxy, tribe_member_age
signature:
docs: docs/re/creature.md#creature-table-cadence-stun
related: BUG-0419
---

## Report
From the K2a audit of BUG-0419. A Tribe member's age is applied only at the mirror's spawn (a baby that grows on the host stays a baby on the guest); a member's carried food is read for the debug print only; an NPC proxy has no befriended flag and no stun state from the host.

## Root cause
Age: runtime, not a bug: a baby the host's game makes grows up on the guest's mirror at the same second (the guest's game grows its own citizen, out/lab/20261107-224702 tribe_member_age, 57 s). Carried food: fixed by BUG-0617 (0.7.9). Befriended: the species table (BUG-0699) and NF_IMPRESSED / NF_PACK. Stun: a host NPC's stun stayed in the host's game, the guests' proxies never had it (h_Stun returns for a proxy).

## Fix
`creature.stun_proxy`: the host sends its NPC's stun (its own and a guest's K_STUN_NPC) as K_STUN_PROXY, each guest's game stuns its proxy with the game's CreatureStun (player_rules.cpp).

## Verification
* FAIL with `SPORE_FIX_OFF=creature.stun_proxy`: out/lab/20261107-230154.
* PASS: out/lab/20261107-225634; tribe_member_age (probe of the age part): out/lab/20261107-224702.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state-b): fixed.
