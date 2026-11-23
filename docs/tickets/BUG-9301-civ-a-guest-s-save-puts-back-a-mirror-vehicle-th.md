---
id: BUG-9301
title: Civ: a guest's save puts back a mirror vehicle the host's state removed during the save (dangling pointer in the civilization's vehicle lists, CIV-1)
status: open
kind: crash
stage: Civ
reported: 2026-11-22
source: audit
role: guest
versions: 0.7.16.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-22: opened.
