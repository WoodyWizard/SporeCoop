---
id: BUG-9221
title: Co-op lobby: even spacing, Stop Hosting / Leave bottom right, Copy Server IP
status: fixed
kind: feature
stage: menu
reported: 2026-11-15
source: owner 2026-11-15
role: all
versions: 0.7.14.1-lineages56-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 79b4806b
tests: manual Windows QA: the MSI, two games
signature:
docs:
related:
---

## Report
The owner (2026-11-15, on a screenshot): "ensure consistent spacing again": Copy Host Address nearly touching the menu border, Settings too close to the bottom of its panel, the World & Session panel's left / right spacing uneven; "usually stop hosting would be the button on the bottom right, not Copy Host Address"; "Copy Host Address could be simplified to Copy Server IP".

## Root cause
Observed: the panel image's boxes are Players 34..388, World & Session 406..725 (both 156..290) and chat 337..407 (data/ui/lineages_lobby_panels.png at 34,156); the panel's buttons ran 445..706 (39 px in on the left, 19 on the right) and down to 290; the last row's button ended 5 px above the window's content edge (581).

## Fix
- One grid (switch ui.lobby_grid): the panel's buttons 422..709 (16 px in on both sides), 28 px tall, 5 px apart, the last 7 px above the box; the bottom rows 32 px tall, 8 px apart, right edge on the boxes' 725; Stop Hosting / Leave bottom right (533..565, 16 px above the edge), Copy Server IP and a guest's Send / Don't Send in the row above; Report Issue on the right edge; the World & Session heading 6 px in like Players.
- Names fit their column by the font's glyph widths (1 unit = 1 px at 768 lines, 116 wide): " (host)" only while the whole name fits with it, else a too-wide name is cut with "..."; the guest's "Connected to <host>" line shows the host's name without the tag, at most 16 letters (one line).

## Verification
FAIL (switch off, MSI host window): the 56-dev layout (out/shots/r58/host_window_off.png, not in git).
PASS (switch on, MSI host and guest windows): the panel buttons 16 px from both box sides, Settings 7 px above the box's bottom, Stop Hosting / Leave bottom right 16 px above the edge.
no-fail-run: Windows QA by hand (no lab on this PC); a layout change, checked on screenshots.

## History
- 2026-11-15: opened.
- 2026-11-15: done.
- 2026-11-15: names fitted by width (MSI shots with a 24-letter host name and an 11-letter guest name).
