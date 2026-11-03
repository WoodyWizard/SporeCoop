---
id: BUG-0492
title: guest crash in Civ at UTFWin::IImageDrawable::SetImageForWindow+0x21F (null +0xC) under cPlanetRecord::CalculateDeltaSpiceProduction, 0.7.3.6
status: open
kind: crash
stage: Civ
reported: 2026-11-03
source: player report SporeCoop-report-20261103-194448.zip (owner downloaded 2026-11-03; 0.7.3.6, Steam guest, Civ), out/reports/2026-11-03
role: guest
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
Guest (Steam), Civ, 19:29:30, 529 s into the session, after the host founded 9 cities in 30 s (civ: the host founded a city ... cities now 9): 0xC0000005 at 0x13AF7AF (data 0xC), chain SetImageForWindow+0x1778 < +0x17BA < cPlanetRecord::CalculateDeltaSpiceProduction+0x268C (frame names misleading, BUG-0346). A UI image (city / spice panel) of an object the guest's game does not have in full (a founded mirror city?) is null.
Target: 0.7.4 (first: these are crashes on the current release).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
