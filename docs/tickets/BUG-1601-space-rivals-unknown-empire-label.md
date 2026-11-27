---
id: BUG-1601
title: Space Rivals: unnamed empires display literal "unknown" placeholder in Rivals UI
status: fixed
kind: bug
stage: Space
reported: 2026-11-26
source: AI playtest (v0.8.2.3)
role: both
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 225ccb98
tests: make test
signature: Space Rivals empire row renders literal "unknown" name
switch: none (display-name sanitizer in space_rivals_logic.h)
docs:
related:
---

## Report
Unnamed or uninitialized Space empires displayed empty or literal `"unknown"` strings in the Space Rivals list.

## Root cause
`space_rivals_logic.h` accepted raw `"unknown"` strings returned by native empire name helpers without replacing them with a readable fallback.

## Fix
Updated `mods/coop/space_rivals_logic.h` to replace empty or literal `"unknown"` empire names with a clean `"Empire #N"` label.

## Verification
- no-fail-run: portable display-name sanitizer verified by `make test`.
