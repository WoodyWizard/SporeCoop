---
id: BUG-9372
title: Galaxy menu: a server planet's panel shows a garbage date
status: open
kind: bug
stage: Galaxy
reported: 2026-11-24
source: deep playtest 2026-11-24
role: guest
versions: 
fixed_in:
branch:
commits:
tests: tools/qa_win (manual, the deep playtest of 2026-11-24)
signature:
docs: docs/known-issues.md#known-galaxy-menu-planet-date
related:
---

## Report
Deep playtest 2026-11-24: in the galaxy menu the panel of a planet of the galaxy server shows a date that is not
one ("12/-67/38" for a planet made that hour, "01/24/00" for two stored ones). The entry the mod writes for a server
planet (galaxy_menu.cpp) leaves the date's fields unset. Cosmetic. Reproduce: connect, click a server planet's star.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-24: opened.
