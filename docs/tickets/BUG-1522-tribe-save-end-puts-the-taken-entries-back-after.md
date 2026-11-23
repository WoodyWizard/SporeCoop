---
id: BUG-1522
title: Tribe: save_end puts the taken entries back after a save without checking that each entry is still alive (CIV-1's bug in Tribe)
status: open
kind: crash
stage: Tribe
reported: 2026-11-23
source: plan
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w79-tribe
commits:
tests:
signature:
docs:
related: BUG-1416
---

## Report
tribe.cpp `save_end` (g_took_t) puts the entries it took out for a save back when the container is alive, but does not check the entry itself, which the host may have destroyed during the save window: BUG-1416 (CIV-1) in Tribe. Test like civ_save_took (a member removed by the host while the guest saves) and a twin. docs/plan-0.7.17.md item 12.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-B1), branch fix/w79-tribe.
