---
id: BUG-0431
title: class: requests and objects the mod builds are incomplete vs the game's own (umbrella)
status: open
kind: bug
stage: Creature
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
Fixed per case: BUG-0368 (EditorRequest +0x94), BUG-0008 (per-reader guards).
Siblings not covered: own editor request vs the game's (0xD42E50): +0x90 = 0x64AA168, bytes +0x39/+0x3B = 0 and +0x3C = 1, collectables vector cPlayer +0x10FC..+0x1108, message 0x5D02A72 after Submit, global 0x1686AF0 (obs); the coop.vote editor test command (coop.cpp:4831) same; Cell and Tribe/Civ/Space editors not checked; tuning readers unguarded for puppets/proxies: callers of 0xC0C300 (0xBA43E0, 0xBA4A00, 0xD9BBD0, 0xC09B40), direct read 0xC115E0 (obs static).
General fix: build requests like the game (byte diff with an allow-list); complete puppets/proxies (tuning block). Outcome check: request diff; every tuning reader runs on a puppet/proxy without fault.
Source: docs/generalisation-plan.md G15.
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
