---
id: BUG-9344
title: Notifications: a long message box runs past the screen's right edge at 1024x768
status: open
kind: bug
stage: menu
reported: 2026-11-22
source: TITAN QA run 2026-11-22 (BUG-9343's switch-off picture)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9339
---

## Report
TITAN QA 9 at 1024x768 (BUG-9343's switch-off run): the message box "Downloading Clyth (Tribe) from the galaxy
(3730 KB)..." stood at the top right with its text running past the window's right edge ("galaxy" cut). BUG-9339's
planet list was the same layout with a longer line. Reproduce: a 1024x768 window, enter a server planet.

## Root cause
Not looked into. Inferred: the notification box is placed at a fixed x and sized to its text without a limit to the
screen's width.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-22: opened.

