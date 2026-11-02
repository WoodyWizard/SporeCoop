---
id: BUG-0419
title: class: state sent but not applied on the receiver (umbrella)
status: open
kind: bug
stage: All
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
Fixed per case: BUG-0373 (Creature partner HP), BUG-0334.
Siblings not covered (obs): Space partner ship health pinned to max (space_drive modes.cpp:323-325); Cell partner cell health pinned to 6 (cell_drive); NpcSpawn.scale never read (npc.cpp:312 vs 1428-1450); NpcSpawn.max_health stored unused (npc.cpp:1441); tribe member age applied only at spawn (tribe.cpp:554/911); PF_JUMPING / PF_SWIMMING / PlayerState.anim dead fields, PF_RUNNING never read; guest applies only pos, hp>0, dead, impressed for proxies (befriended, stun, size missing); BUG-0173 dead flag +0xB5E, action, held tool not sent; status effects (poison, sprint, sneak, glide) and tribe tool held / carried food / task not sent (inf).
General fix: one table of every state field per object kind with its apply site per stage. Outcome check: sync-check digest carries state fields (hp, max, dead, scale, age); unit test fails on an unlisted field.
Source: docs/generalisation-plan.md G2.
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
