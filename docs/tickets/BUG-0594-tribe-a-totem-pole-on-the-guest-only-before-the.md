---
id: BUG-0594
title: Tribe: a totem pole on the guest only before the move to Civ (tribe_civ, detector guest_only:totem_pole)
status: open
kind: desync
stage: Tribe
reported: 2026-11-05
source: detector, rel/0.7.7 final check out/lab/20261105-015320 and 015350
role: guest
versions: 0.7.7-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
tribe_civ (3 samples): "1 totem_pole on the guest, none on the host (at (404 8 305))", "totem_pole: 0 in the
host's game, 1 in the guest's (whole list)". Reproduce: `SPORE_DETECTOR=20 tools/lab.py run tribe_civ`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
