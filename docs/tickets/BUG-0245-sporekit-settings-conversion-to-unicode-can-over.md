---
id: BUG-0245
title: SporeKit: settings conversion to Unicode can overwrite a UTF-16 BE coop.ini with garbage, and two instances can lose a setting
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #179 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-net
commits: 6fa9165, f34f769, 005f6ba
tests: tests/win_tests.cpp test_ini_encoding (win_unit --old-iniencode, --old-inilock fail)
signature:
docs: docs/02-modkit.md (kit.fix)
switch: kit.iniencode, kit.inilock
related:
---

## Report
From GitHub issue #179: https://github.com/WoodyWizard/SporeCoop/issues/179 (the full analysis, links and the scenario are there).

**Summary**
Since BUG-0142, SporeKit rewrites a `mods\*.ini` file as UTF-16 LE the first time it reads non-ASCII text in it. Only a UTF-16 LE byte order mark is recognised: a file saved as UTF-16 BE (Notepad offers it), or UTF-16 without a BOM, is decoded as the ANSI codepage and saved over the original, so the player name, the last address and every other setting in it are lost. Separately, two game instances on one folder share one temporary file name: while one converts, the other's non-ASCII write is stored as "????", and a write between the other's read and rename is overwritten. Severity: data loss of a settings file (rare trigger) and a lost setting (PLAUSIBLE). Introduced in release 0.7.3.0 by BUG-0142 (commit 363db2b).

**Where**
- `make_unicode`: https://github.com/WoodyWizard/Sp

## Root cause
Observed in win_tests: make_unicode knew only FF FE, so a UTF-16 BE file was decoded as the ANSI codepage and written over the original (name empty, share_world lost); a UTF-16 file without BOM was converted on a guess; with the fixed temp name held by another instance the write went to the ANSI file as "????".

## Fix
make_unicode: FE FF is byte-swapped losslessly; a file with NUL bytes is left as it is (logged once); a write re-checks the file instead of trusting the per-run cache. A named mutex per file ("SporeKitIni-", sc_lock_name, 500 ms, g_mu first) around the conversion and every write; a non-ASCII value is not written when the file could not be made Unicode (write_str returns false, the co-op window shows "could not save the name, try again"). kit.fix iniencode / inilock. Not done: the tests' ini_append helper (tools append ASCII to coop.ini; a UTF-16 coop.ini on a lane would be corrupted by them, as before).

## Verification
win_unit lane 6 PASS out/lab/20261028-230252 (BE read as "Вася", share_world 1, file now FF FE; no-BOM file unchanged; held temp: no "????", next write stores the name; held lock: refused). `--old-iniencode` FAIL (3), `--old-inilock` FAIL (2: "????").

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-net (wave 5).
- 2026-10-29: released in 0.7.3.1.
