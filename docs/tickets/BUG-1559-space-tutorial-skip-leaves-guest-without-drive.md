---
id: BUG-1559
title: Space: skipping the Space tutorial on host leaves guest without interstellar drive and tutorial unlock
status: open
kind: bug
stage: Space
reported: 2026-11-25
source: GitHub #221 (Dedlection)
role: both
versions: 0.7.14.1, 0.7.15.5, 0.8.0.1
fixed_in:
branch: w0802/space-tut-drive
commits:
tests: space_tut_drive, space_tut_drive_off
signature:
docs: docs/re/space.md
related: BUG-0181, BUG-1539
---

## Report
GitHub #221 (`Dedlection`): if the host skips the Space stage tutorial (or finishes it in-session), the guest is not granted the interstellar drive (`0x2569FAE8`) and remains locked inside the home star system unable to travel to other stars.

## Root cause
Pending investigation in `mods/coop/spacecoop.cpp` (`cMissionManager` `+0x16/+0x17/+0x18` tutorial flags and `interstellardrive` `0x2569FAE8` synchronization).

## Fix
Pending implementation behind `COOP_FIX("space.tutorial_drive_sync", "BUG-1559")`.

## Verification
Pending Wine lab verification (`space_tut_drive` PASS + `space_tut_drive_off` FAIL).
