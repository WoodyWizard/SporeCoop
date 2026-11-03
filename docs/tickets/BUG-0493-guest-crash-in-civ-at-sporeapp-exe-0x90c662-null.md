---
id: BUG-0493
title: guest crash in Civ at SporeApp.exe+0x90C662 (null +0x2C) near cRelationshipManager::DeclareAlliance, 0.7.3.6
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
Guest (Steam), Civ, 19:44:47, 1447 s into the session, 13 s after a partner's city was under attack: 0xC0000005 at 0x128C662 (3010a413 +0x90C662, read 0x2C), chain DeclareAlliance+0x12B0 < ? < UTFWin::Window::FindWindowByID < App::Canvas::func6Ch. Third crash of the same guest in 15 minutes (with BUG-0491 and BUG-0492 above): the Civ guest path has a lifetime problem (class G9).
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
