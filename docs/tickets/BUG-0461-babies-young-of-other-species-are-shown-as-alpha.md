---
id: BUG-0461
title: babies (young) of other species are shown as alphas (adult size) on the guest
status: open
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
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
Guest's mark (s 1926): 'i see babies from other species as alphas'. The generalisation audit found NpcSpawn.scale is sent and never read (npc.cpp:312 vs 1428-1450): proxies get the default size. Class G2 (BUG-0419).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
