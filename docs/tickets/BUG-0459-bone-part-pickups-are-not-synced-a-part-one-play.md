---
id: BUG-0459
title: bone / part pickups are not synced: a part one player picks up is still there for the other, so both unlock 2 parts instead of 1
status: open
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
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
Host's mark (s 1810): 'after guest picks up a bone its not picked up on my screen which results in unlocking 2 parts instead of 1 for both'. The pickup objects (skeletons / part rewards) are each game's own; with parts.shared (BUG-0401) the second pickup unlocks again. Fix: host-authoritative pickups (remove on all games), class G4 (BUG-0421).
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
