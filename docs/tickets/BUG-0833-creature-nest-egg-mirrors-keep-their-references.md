---
id: BUG-0833
title: Creature: nest egg mirrors keep their references to the egg and its herd at every guest world change (suspected leak)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-creature
commits: c6bcdd15
tests: nest_mirror_epoch
signature:
docs: docs/re/creature.md#creature-c2-audit-0713, docs/06-testing.md#testing-c2-creature-0713
related: BUG-0384 BUG-0426 BUG-0621
---

## Report
No player report. Suspected: each load, stage change or own editor of a guest leaks up to 20 eggs and their herds (address space over a long session).

## Root cause
Observed at runtime (confirms the code reading): after the host sends its world again (`coop.share`), the guest's
old mirror eggs are destroyed by the load (+0x21 set) but their blocks stay alive: 4 references with the mod's, 3 of
the game's own (lab 20261112-074517, -075547, -080216). The world counter also goes up when the guest's avatar is
replaced inside the stage, and then the mirrors were only forgotten while still listed (two eggs per host egg).

Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): nest.cpp make_mirror (226, 229) AddRefs the egg and the NPC nest herd; on_world_change (597-603) and the epoch branch of guest_tick (327-333) only null the pointers; remove_mirror (248-262) and the !mirror_ok path (340-343) skip the Release once the epoch changed or +0x21 is set. Not checked: the teardown's view of a ref-held egg. Confidence: low.

## Fix
nest.cpp drop_old_mirror: at a world or avatar change (the epoch) and when the game took a mirror, a mirror still
listed with our serial is removed as remove_mirror does (herd reference, DestroyInstance, our reference); one the game
destroyed with its world gives back the herd's and our reference. Switch `coop.fix nest.mirror_epoch off`: forgotten
as in 0.7.11. Not removed: the 3 references the game itself keeps on a destroyed egg (not ours, not looked into).

## Fix idea
Release our two references in remove_mirror also after a world change while the object still has our serial.

## How to test
Guest near 20 egg mirrors (`coop.nest objs`), reload the world N times; census of noun 0x2A034CD and herds must stay flat (fix) vs grow (main).

## Verification
Test `nest_mirror_epoch` (coop_nest_mirror_epoch.py): the host's herd lays two eggs at a nest of another species, the
guest mirrors them, `coop.share`; 10 s after the load each old mirror egg's refcount (+8) must not be above the 3 the
game keeps.
* FAIL: out/lab/20261112-080216 (`SPORE_FIX_OFF=nest.mirror_epoch`, lane 6): refcount 4 on both.
* PASS: out/lab/20261112-080250 (lane 7) and out/lab/20261112-080849 (lane 6): 3.
* (The previous check "fell by 2 since before the load" passed with the switch off, lab 20261112-075547: a live
  mirror egg holds ~19 references.)

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-creature, C2 of 0.7.13): fixed (nest.mirror_epoch); FAIL 20261112-080216, PASS 20261112-080250, 20261112-080849.
