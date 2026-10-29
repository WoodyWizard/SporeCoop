---
id: BUG-0241
title: Updater: every in-game update attempt leaks the whole downloaded pack (several MB) because the update threads exit inside the scope of their locals
status: open
kind: bug
stage: Installer
reported: 2026-10-28
source: GitHub #175 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
From GitHub issue #175: https://github.com/WoodyWizard/SporeCoop/issues/175 (the full analysis, links and the scenario are there).

**Summary**
Each in-game update attempt (Update, Try again) keeps the decoded update pack in memory for the rest of the game, and each update check leaks its release record. The pack holds every file of the release (coop.dll, sporekit.dll, the loader, SporeCoop.package, the symbol files), so every failed-and-retried update costs several MB in a 32-bit game where running out of memory is already a known crash cause (BUG-0127). Severity: minor (memory). Introduced in release 0.7.3.0 by the BUG-0060 fix (commit 2bb6835), which made both threads leave through a `[[noreturn]]` helper.

**Where**
- `end_thread`: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/mods/coop/autoupdate.cpp#L68-L75
- `check_thread`: https://github.com/WoodyWizard/SporeCoop/blob/58

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
