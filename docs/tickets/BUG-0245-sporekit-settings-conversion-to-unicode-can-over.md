---
id: BUG-0245
title: SporeKit: settings conversion to Unicode can overwrite a UTF-16 BE coop.ini with garbage, and two instances can lose a setting
status: open
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #179 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #179: https://github.com/WoodyWizard/SporeCoop/issues/179 (the full analysis, links and the scenario are there).

**Summary**
Since BUG-0142, SporeKit rewrites a `mods\*.ini` file as UTF-16 LE the first time it reads non-ASCII text in it. Only a UTF-16 LE byte order mark is recognised: a file saved as UTF-16 BE (Notepad offers it), or UTF-16 without a BOM, is decoded as the ANSI codepage and saved over the original, so the player name, the last address and every other setting in it are lost. Separately, two game instances on one folder share one temporary file name: while one converts, the other's non-ASCII write is stored as "????", and a write between the other's read and rename is overwritten. Severity: data loss of a settings file (rare trigger) and a lost setting (PLAUSIBLE). Introduced in release 0.7.3.0 by BUG-0142 (commit 363db2b).

**Where**
- `make_unicode`: https://github.com/WoodyWizard/Sp

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
