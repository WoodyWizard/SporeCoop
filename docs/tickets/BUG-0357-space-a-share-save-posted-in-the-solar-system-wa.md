---
id: BUG-0357
title: space: a share save posted in the solar system was written after the host landed on a planet: the guest that loaded it faults every frame at 0xEBDD12 (and 0xFD9420)
status: open
kind: bug
stage: space
reported: 2026-10-31
source: lab 20261031-185843 campaign_space#3 (lane 7)
role: guest
versions: main 55da78b
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
- 2026-10-31: opened.
