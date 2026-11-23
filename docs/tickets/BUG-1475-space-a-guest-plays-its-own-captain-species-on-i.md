---
id: BUG-1475
title: Space: a guest plays its own captain species on its own game (#217 a by IltonBJSilva)
status: open
kind: feature
stage: Space
reported: 2026-11-23
source: plan, PR #217
role: guest
versions: 0.7.17
fixed_in:
branch: feat/w74-captain
commits:
tests:
signature:
docs:
related: BUG-1111, BUG-1262, BUG-1279, BUG-1237, BUG-1321
---

## Report
The 0.7.16 decision "captain and crew later" ends here: cEmpire_SetCaptainKey (+0xC4) on the guest's own game, put back for every save write (the species-line rule), through BUG-1111's guard path; creations 1.2 MSG_CREATION kind 11 (reserved in WP-0). docs/plan-0.7.17.md item 7. Credit IltonBJSilva (PR #217).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 7 (WP-K1), branch feat/w74-captain.
