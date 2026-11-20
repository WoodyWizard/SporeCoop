---
id: BUG-9270
title: A guest's own nest herd keeps the host's family copies in its member list after the guest joins it
status: open
kind: bug
stage: Creature
reported: 2026-11-20
source: Lineages port test (lane A)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9262
---

## Report
Lane A 2026-11-20: on the guest's game, its own nest herd (`coop.nest herd <home>`) lists 9 members: the guest, its 4
family copies and 4 copies of the host's family (species 065CBE70, standing at the host's nest 60 m away).

## Root cause
Observed: own_family_tick moves those 4 copies to their species' nest herd (`#18 (another species) leaves our own nest
herd ...`, Creature_SetHerd) and `coop.npc list` then shows them in that herd (herd=281D29A8), yet the guest's own herd's
member list (+0x40) still names them. Inferred: CreatureGame_JoinNestHerd (the claim) carries the avatar's old herd
members into the new herd's list, and Creature_SetHerd removes a creature only from the herd its +0x1674 names.
Effect not seen: the game's family logic reads +0x40 (mates, greetings); no wrong behaviour was observed in the test.

## Fix
Not fixed: needs the herd's remove function (0xC6B5E0, called by Creature_SetHerd; no symbol yet) checked first.

## Verification
<!-- the test: FAIL without the fix, PASS with it -->

## History
- 2026-11-20: opened (found in the Lineages port test, lane A).
