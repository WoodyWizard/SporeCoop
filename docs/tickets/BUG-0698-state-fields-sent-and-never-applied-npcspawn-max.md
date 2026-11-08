---
id: BUG-0698
title: State fields sent and never applied: NpcSpawn.max_health, PlayerState PF_RUNNING / PF_SWIMMING / anim
status: open
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0419 (0.7.11)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0419
---

## Report
From the K2a audit of BUG-0419. NpcSpawn.max_health is sent and stored (px.max_health) and never read; PlayerState PF_RUNNING is set and never read, PF_SWIMMING never set or read, PlayerState.anim always 0 (animations go by MSG_ANIM). State the receiver never applies, or wire bytes that mean nothing.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
