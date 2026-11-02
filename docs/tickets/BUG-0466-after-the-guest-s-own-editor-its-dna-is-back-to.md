---
id: BUG-0466
title: after the guest's own editor its DNA is back to what it had before (DNA spent in the editor is not kept)
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
Guest's mark (s 1062, 18:21): 'after editor i still have the same amount of DNA as when I went in'. The audit found (G6, BUG-0423): shared.cpp sets have=false on a mode mismatch and drops the local delta, so a spend right after an editor is undone (free DNA). Class G6.
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
