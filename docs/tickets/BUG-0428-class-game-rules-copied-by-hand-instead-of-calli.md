---
id: BUG-0428
title: class: game rules copied by hand instead of calling the game (umbrella)
status: open
kind: bug
stage: Cell
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Fixed per case: BUG-0078, 0240 (hand-copied DamageCell gate), BUG-0001 bites (hook list).
Siblings not covered: Cell electric / poison area attack 0xE71520 (from 0xE71980 @0xE71BCA) writes hp at 0xE71915 without DamageCell 0xE7A7C0 / KillIfDead: a guest's electric/poison parts never hurt host cells, no meat (obs static); guest lethal bite ACT_BREAK skips the gate (cellsync.cpp ~3312) (inf); damage from a guest's non-avatar attacker dropped (inf); Creature guest hits skip the whole TakeDamage on the guest (npc.cpp:1544-1559): hit feedback lost (BUG-0350 fixed only the Cell ring).
General fix: call the game's own function with the puppet as actor, or forward hp at the write point. Outcome check: every damage type from the guest: host hp and death, guest feedback state.
Source: docs/generalisation-plan.md G12.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
