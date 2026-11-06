---
id: BUG-0612
title: class: the rest of BUG-0425 that is not the placing or timing of a remote body (one CycleTracker, NaN guards, oversize messages, Space ship caps, root motion, hops)
status: open
kind: bug
stage: All
reported: 2026-11-06
source: BUG-0425 umbrella (split by fix/w45-remote-bodies)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0425
---

## Report
Split from the umbrella BUG-0425 when its placing / timing part was fixed (fix/w45-remote-bodies, drive.lead). Its
other parts, from docs/generalisation-plan.md G8, are not about where a remote body is drawn and were not touched:
Civ / Tribe accept a late chunk of an OLDER cycle (civ.cpp, tribe.cpp: objects recreated, one CycleTracker wanted);
no NaN / Inf guard on received poses outside Cell (game.cpp game::teleport / set_position / place); oversize
unreliable messages dropped silently (connection.cpp); Space ship mirrors without a speed cap / extrapolation
limit (squadron.cpp defaults, BUG-0190, BUG-0388); root motion of abilities (BUG-0414); hops (BUG-0376).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
