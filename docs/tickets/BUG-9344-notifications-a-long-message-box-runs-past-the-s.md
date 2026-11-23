---
id: BUG-9344
title: Notifications: a long message box runs past the screen's right edge at 1024x768
status: wontfix
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
~ superseded (2026-11-23): "Not looked into. Inferred: the notification box is placed at a fixed x and sized to its text
without a limit to the screen's width."
CORRECTION (2026-11-23, feat/lineages-alpha): the box is not too wide. notify.cpp places every panel at x = screen
width - 560 - 24 (440 at 1024x768, 560 wide: it ends at 1000) and slides it in from the right edge over 0.25 s. At the
QA copies' 10 FPS that slide is 2 or 3 frames, and a picture taken then shows it part way in. TITAN QA 9 at 1024x768,
entering Clyth from the galaxy, `coop.notify` read every 1.5 s: the "Downloading Clyth (Tribe)..." panel at
(574 115 1134 173), one read later at (440 115 1000 173); "hosting on port" at (802 363 1362 421), then (440 ...). In
place, the same text fits in two lines inside the panel (scratchpad n1.png, not kept). The bug picture was such a frame.

## Fix
None: not a bug (the slide-in seen at 10 FPS). At a player's frame rate the slide lasts 0.25 s.

## Verification
no-fail-run: not a bug; the panel positions above show it slide in and stop inside the screen.

## History
- 2026-11-22: opened.
- 2026-11-23: wontfix: the panel was caught during its slide-in (10 FPS); in place it fits the screen.
