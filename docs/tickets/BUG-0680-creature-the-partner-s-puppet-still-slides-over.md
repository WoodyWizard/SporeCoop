---
id: BUG-0680
title: Creature: the partner's puppet still slides over 0.3 m past where the partner stopped at about 1 stop in 12 (rest of BUG-0408; puppet_stops flaky)
status: open
kind: desync
stage: creature
reported: 2026-11-07
source: lab 20261107-132459 (A/B: rel/0.7.9 1/4 PASS, rel/0.7.10 3/4 PASS) and 20261107-124806
role: 
versions: 0.7.9, rel/0.7.10
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0408
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
- 2026-11-07: opened.

Observed: puppet_stops x4 on each, same time, lanes 4/5: v0.7.9 1/4 PASS (fails: 1 of 14, 1 of 11, 1 of 10 stops over 0.3 m, both directions), rel/0.7.10 3/4 PASS (the fail: too few stops to judge). Not a 0.7.10 regression; BUG-0408 holds for most stops but not all.
