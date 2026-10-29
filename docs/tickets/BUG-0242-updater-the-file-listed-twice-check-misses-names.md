---
id: BUG-0242
title: Updater: the "file listed twice" check misses names that Windows treats as the same file ("coop.dll" and "coop.dll.")
status: open
kind: desync
stage: Installer
reported: 2026-10-28
source: GitHub #176 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #176: https://github.com/WoodyWizard/SporeCoop/issues/176 (the full analysis, links and the scenario are there).

**Summary**
The BUG-0089 fix rejects an update pack that lists one file twice, but it compares the paths only after lowercasing ASCII letters. `allowed_path` accepts path components that end in a dot, and Windows strips a trailing dot, so `SporebinEP1/mods/coop.dll` and `SporebinEP1/mods/coop.dll.` (or `.../sporekit./symbols/x.sym`) are the same file but pass both checks. Such a pack reaches `apply_pack`, where the second entry can still lose the original file on a rollback, the outcome BUG-0089 was about. Severity: minor (needs a pack with such a path, for example from a packaging mistake). Left incomplete by BUG-0089 (commit 6e8e765) in release 0.7.3.0.

**Where**
- `allowed_path`: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/updater/pack.cpp#L10

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
