---
id: BUG-0377
title: creature: in npc_far / npc_pooled the guest shows the host's puppet about 10.7 m from the host (sync-check p0 apart)
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: lab log gates report over recorded runs (feat/w30-lab-gates, 2026-11-01)
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
`lab.py gates` over recorded results: npc_far 15 of 15 passed runs and npc_pooled 9 of 11 have `sync-check: MISMATCH ... p0 10.7` apart on the guest. Consistent, not triaged. Reproduce: `lab.py run npc_far` with the gates on.
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
