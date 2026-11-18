---
id: BUG-1124
title: A1: a non-ASCII Windows user name is not masked in logs and problem reports
status: fixed
kind: bug
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-hygiene
commits: 28e2259c
tests: make test (unit: tests/path_mask_tests.cpp, path_mask_tests --old)
signature:
docs: docs/audit-0.7.15/kit_build.md (KIT-7)
related: BUG-0142, BUG-1011
switch: none (privacy: the old behaviour leaks the path; the twin is path_mask_tests --old)
---

## Report
A1 of docs/plan-0.7.15.md (KIT-7 of the 0.7.15 audit): a Windows user name with letters outside ASCII (a Cyrillic name in the profile
folder of drive C) stayed in full in sporekit.log and in problem report archives, which players attach to
public issues. Documents moved outside the profile (another drive, OneDrive) were not masked either.

## Root cause
Observed in the code (sporekit/log.cpp before the fix): `init_masks` read USERPROFILE and WINEHOMEDIR with
`GetEnvironmentVariableA`, which returns the ANSI code page (cp1251 on a Russian Windows), while every log and report
line is UTF-8 (`narrow`). The bytes never matched, so the profile path was not replaced. `same_path_char` also folded
ASCII letters only, so another case of a non-ASCII name did not match either. The zip packed the log files as they
were, also logs of earlier runs written before any fix.

## Fix
* sporekit/path_mask.h (new, portable): the masks with a UTF-8 aware, case-folded compare (ASCII, Latin-1,
  Latin Extended-A, Greek, Cyrillic; `/` = `\`); bytes that are no UTF-8 compare as they are; a mask never starts
  inside a letter; longest path first.
* sporekit/log.cpp: USERPROFILE and WINEHOMEDIR through `GetEnvironmentVariableW` + `narrow`; the ANSI form and the
  8.3 short form of the profile are masks too (text written through an A function); the Documents folder
  (`SHGetFolderPathW(CSIDL_PERSONAL)`) becomes `<Documents>` when it is not inside the profile.
* sporekit/diag.cpp: the `.log` / `.txt` files of a report archive and the settings files are masked when they are
  packed (also logs of earlier runs).
* No coop.fix / kit.fix switch: the old path leaks a private path; the FAIL twin is the unit test's `--old` mode
  (the masks set up as before: the ANSI read).

## Verification
* FAIL: `build/host/path_mask_tests --old` (the profile mask in cp1251 bytes, as GetEnvironmentVariableA gave it):
  5 FAILED (the Cyrillic name stays in the line, in another case, and next to the game folder). `make test` requires
  it to fail.
* PASS: `build/host/path_mask_tests`: all passed (Cyrillic name in UTF-8, upper case, forward slashes, the ANSI form,
  Latin-1 / Latin Extended-A / Greek names without case, invalid UTF-8, ASCII paths as before, Documents inside or
  outside the profile). In `make test` (unit-cpp).
* Not run: the Windows side under Wine with a Cyrillic USERPROFILE (win_unit on a lane; this package had
  no lane). The W API read and the report zip are build-checked only.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-hygiene (28e2259c): wide reads, UTF-8 case-folded masks, masked archive texts.
- 2026-11-18: released in 0.7.15.
