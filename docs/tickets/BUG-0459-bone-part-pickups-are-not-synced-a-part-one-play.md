---
id: BUG-0459
title: bone / part pickups are not synced: a part one player picks up is still there for the other, so both unlock 2 parts instead of 1
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 
fixed_in: 0.7.4
branch: fix/w40-world-objects
commits: 9445045a 5ec41dc0 ffed3f29 0209a1c2
tests: wobj_ornament (tools/tests/coop_world_objects.py), parts_shared
signature:
docs: docs/re/creature.md#creature-world-objects, docs/06-testing.md#testing-world-objects
related:
switch: wobj.ornament
---

## Report
Host's mark (s 1810): 'after guest picks up a bone its not picked up on my screen which results in unlocking 2 parts instead of 1 for both'. The pickup objects (skeletons / part rewards) are each game's own; with parts.shared (BUG-0401) the second pickup unlocks again. Fix: host-authoritative pickups (remove on all games), class G4 (BUG-0421).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lane 9): a bone / skeleton is a cInteractiveOrnament from the save (the same object, place and serial on
every game); its part reward is a separate record per game (CreatureReward_Find 0xD475E0, +0x24 used up), paid
by 0xD46BE0 after the interact node. The ornament itself does not change. Each game paid its own reward, and
parts.shared (the union) then gave everybody both parts: with the fix off the test went 54 -> 56 parts on both
games for one bone.

## Fix
worldobj.cpp kind `interactive_ornament` (ornaments with a reward, Creature): the guest's use (its reward used up)
goes to the host as a request; the host marks its reward used up without paying it (+0x10 = 0, +0x24 = 1, +0xA3 =
0) and every guest gets the same with the next snapshot. Switch `wobj.ornament`.

## Verification
`coop_world_objects.py --kind ornament` (suite wobj_ornament, creature fixture): the guest uses a bone with a part
reward (the interact node's end), the host's reward is used up, the host's avatar uses the same bone.
* FAIL with `SPORE_FIX_OFF=wobj.ornament`: out/lab/20261103-123009 (the host's reward stays unused; parts 54 -> 56 on
  both games: two parts for one bone, the report).
* PASS: out/lab/20261103-122753 (54 -> 55 on both).

## History
- 2026-11-02: opened.
- 2026-11-03: the detector sees the pickups themselves differ: `Creature:host_only:interactive_ornament` (bones / skeletons only in the host's game) in 6% of Creature comparisons (out/lab/20261103-012624).
- 2026-11-03: fix/w40-world-objects: fixed (worldobj.cpp / nest.cpp).
- 2026-11-03: released in 0.7.4.
