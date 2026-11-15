---
id: BUG-9220
title: Co-op window: grey and blue buttons (navigation blue, helpers grey, the color button grey); 'color' wording
status: fixed
kind: feature
stage: menu
reported: 2026-11-15
source: owner 2026-11-15
role: all
versions: 0.7.14.1-lineages56-dev
fixed_in: 0.7.14.1-lineages57-dev
branch: feat/lineages-47
commits: 6e8f678e
tests: manual Windows QA: the MSI, two games (host MSICivHost, guest MSIGuest), Abber.spo
signature:
docs:
related:
---

## Report
The owner (2026-11-15): "The color button shows as a yellow button. We have a red / yellow, we should probably add a grey (2B2F36) and a blue. Put them in appropriate locations for our buttons. Reword colour -> color and change the yellow background of the color button to the grey."

## Root cause
Not a bug: every button of the mod drew the gold look except the red ones (BUG-9179).

## Fix
- tools/make_icons.py draws lineages_button_grey_* (around #2B2F36) and lineages_button_blue_*; coop_ui.cpp picks them by the button's name (switch ui.button_roles): blue for page buttons (Settings, Space / Nations >, < Lobby, the page arrows), grey for helpers (the color button, Copy Host Address, chat and choice arrows, Refresh, Save Server, Check for Updates, Don't Send).
- The player-visible "colour" is "color" (the color button's tooltip, "Your color", "another player has this color", the partner map setting).

## Verification
FAIL (switch off, MSI host window): every button gold, the color button a gold pill.
PASS (switch on, MSI host and guest windows): Settings and Nations > blue, Copy Host Address, the role arrows and the color button grey.
no-fail-run: Windows QA by hand (no lab on this PC); a look change, checked on screenshots.

## History
- 2026-11-15: opened.
- 2026-11-15: done and checked on the MSI.
