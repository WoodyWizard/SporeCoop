---
id: BUG-0241
title: Updater: every in-game update attempt leaks the whole downloaded pack (several MB) because the update threads exit inside the scope of their locals
status: fixed
kind: bug
stage: Installer
reported: 2026-10-28
source: GitHub #175 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-net
commits: d127b18
tests: tools/tests/coop_update_leak.py (suite update_leak); SporeKit command mem.commit
signature:
docs: docs/06-testing.md#testing-update-leak
switch: core.update_unwind
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
Observed in the game (lane 6): check_thread / apply_thread called the [[noreturn]] end_thread (FreeLibraryAndExitThread / ExitThread) with their locals in scope; ExitThread does not unwind the C++ stack, so the decoded pack (upd::Pack) leaked at every attempt.

## Fix
The work of each thread runs in check_work / apply_work, whose locals are destroyed on return; the thread functions only call the work and end_thread. Switch `coop.fix core.update_unwind off` = end_thread inside the work (the leak).

## Verification
update_leak on lane 6: PASS out/lab/20261028-224927 (committed private memory 231 -> 211 MB after 5 refused attempts with a 20 MB pack); FAIL with SPORE_FIX_OFF=core.update_unwind out/lab/20261028-225154 (233 -> 326 MB, +93 MB).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-net (wave 5).
