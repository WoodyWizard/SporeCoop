---
id: BUG-0595
title: Space: the guest's money reads nan in the detector's state row (campaign_space)
status: open
kind: desync
stage: Space
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
campaign_space (3 samples): "money 100000 on the host, nan on the guest". Either the guest's money field is
read before its Space empire exists (a detector read) or the guest's value is really not a number. Reproduce:
`SPORE_DETECTOR=20 tools/lab.py run campaign_space`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
