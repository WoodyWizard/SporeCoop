---
id: BUG-0322
title: tests: tribe_npc_dash fails about half of the runs on a loaded machine (p90 1.3-2.0 m, limit 1.2 m)
status: duplicate
kind: test
stage: Tribe
reported: 2026-10-30
source: fix/w13-creature lab runs 2026-10-30
role: 
versions: 
fixed_in:
branch:
commits:
tests: tribe_npc_dash
signature:
docs:
related: BUG-0305, BUG-0321
---

## Report
Lanes 4 and 5, 2026-10-30, 5 agents' tests on the machine at once: `tribe_npc_dash` (BUG-0305's test,
limit p90 1.2 m) failed in 4 of 6 runs on fix/w13-creature: p90 1.32-1.98 m (out/lab/20261030-141545,
20261030-145052, 20261030-150139), and in 3 of 5 with that branch's new fixes switched off
(out/lab/20261030-145735, 20261030-150145: 1.47, 1.32, 1.87 m; a pass at 0.93 m). Not caused by that
branch (same spread both ways). BUG-0305 measured 0.7-0.95 m with the fix: likely the machine's load
(the guest is limited to 15 fps by the test, the host's frame rate is not).
Next: run it alone on an idle machine; if it passes there, the limit or the host's frame rate needs
a look.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w14-tribe-civ): duplicate of BUG-0321 (the same test and symptom); the analysis and the change are there.
