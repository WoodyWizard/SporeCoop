---
id: BUG-9261
title: Loading cover looks low resolution at 1440p: blocky blur, small text
status: fixed
kind: bug
stage: All
reported: 2026-11-19
source: owner play test 2026-11-18
role: guest
versions: 0.7.14.1-lineages65-dev
fixed_in: 0.7.14.1-lineages66-dev
branch: feat/lineages-47
commits: c3ba2fbe
tests: tools/qa_win/qa.py (manual: MSI guest at 2560x1440 joining, shot)
signature: loading cover blocky and its text tiny at 2560x1440
docs:
related: BUG-9202, BUG-9227, BUG-9159
---

## Report
Owner 2026-11-18: "the loading screens aren't centered and seem to be low resolution."

## Root cause
Observed on the MSI at 2560x1440: (1) ring_timer::loading blurred by copying the frame to 1/4 and 1/16 and back in two
steps; at 1440p the 160x90 copy came back as visible blocks. (2) The cover's text used SporeKit's only overlay font
(Arial bold 18 px), tiny at 1440p, and was centred by a guess of 9 px per letter.

## Fix
ring_timer.cpp: the frame halves four times (1/2 ... 1/16) and comes back the same way, each step filtered. SporeKit
0.3.18 adds `overlay_text_px` (text in a font of any height, or its measured width); coop.cpp cover_lines draws the
covers' title and step at 22 / 18 px per 1080 lines of screen, centred by the measured width (the join, nest and
editor cover, "Paused by host", the late village cover). Switch: `coop.fix ui.cover_text_scale off` (the 18 px font);
an older SporeKit falls back to it too.

## Verification
no-fail-run: a picture. Manual 2026-11-19 on the MSI: a guest at 2560x1440 joining (the cover smooth, title and step
sharp and centred under the ring), a guest at 1600x900 with the host paused ("Paused by host" centred).

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on feat/lineages-47 (c3ba2fbe).
