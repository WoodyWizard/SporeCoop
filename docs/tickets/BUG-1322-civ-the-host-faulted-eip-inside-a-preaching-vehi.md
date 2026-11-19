---
id: BUG-1322
title: Civ: the host faulted (EIP inside a preaching vehicle, from 0xBFD8DB) with test preachers put 30 m over the ground
status: open
kind: crash
stage: Civ
reported: 2026-11-19
source: lab
role: host
versions: 0.7.16 (unreleased)
fixed_in:
branch: fix/w57-sync
commits:
tests: civ_preach_far
signature: exception 0xC0000005 at a heap address inside a vehicle object, stack scan 00BFD8DB 00AE75E0
docs: docs/re/civ.md#civ-npc-fixture-slots
related: BUG-1286
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->
Lab run out/lab/20261119-114030 (fix/w57-sync worktree, lane 9), civ_preach_far: the host spawned three religious
vehicles past the civilization's allowance (`coop.civ spawnveh ... force`, the civ_npc fixture allows none) at
207E4128 / 208134E0 / 20841300, put them 80-100 m from the far NPC city along x (about 30 m over the ground: the old
put_near) and ordered a religious attack. 3 s later: `exception 0xC0000005 at 0x207E4131 (data 0x01464301)`, i.e. EIP
inside the first vehicle object (+9), then one at 0xCCCCCCCC; the stack scan starts 00BFD8DB (FUN_00bfd750, a
combatant's target-point list: owner vt+8, vt+0xAC / +0x34 / +0x68) 00AE75E0 00BEAA93; 191 faults counted in all, the
test went on. Two of the three preachers were gone on the host afterwards (in other runs too, without a fault).
Not seen in 3 other runs with the vehicles in the air, nor in 2 runs with them on the ground (20261119-115145).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->
Not known. Inferred: a vtable call through an object whose vtable slot holds a vehicle pointer, i.e. a stale or wrong
combatant / owner pointer after a preacher was taken away (the city's retaliation 0xBEB6F8 or a disband past the
allowance). The fault came 90 ms before the BUG-1286 hook set the slot's places for the first time, so that hook is not
in it by timing. Whether a player can get there (vehicles only from the game's buy path, on the ground) is open.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->
None in the mod. The test puts its preachers on the ground now (put_near on the city's sphere). Next: if it comes again,
a crash dump of FUN_00bfd750's receiver (which combatant, which owner).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened.
