---
id: BUG-9319
title: Tribe: the partner rings are drawn over the Spore Guide window
status: open
kind: bug
stage: Tribe
reported: 2026-11-21
source: QA 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev (fix/l47-tribe 3e6d1b0a)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Live QA 2026-11-21 (MSI two games, host MSITribeHost instance 2, guest MSIGuest instance 1, Tribe from Vandh): the
host pressed H (the Spore Guide window opened over the whole screen) and the blue partner rings under the partner's
villagers stayed drawn on top of the guide's text (out/qa-shots/rings_over_guide_host.png). `coop.status` at that
moment: `labels: menu_up=0 window=0 ... partner_rings=3`.
Repro: Tribe with a partner's villagers on screen, press H (or the pause menu's Spore Guide button).

## Root cause
Observed in the code (coop.cpp menu_check_task): the world labels (tags, rings, arrows) are hidden only for the pause
menu (0x0615B50D), the co-op window, a game message box (0x06318E78) and a cinematic that holds the game; the Spore
Guide window is none of these, so `labels` stays true. The guide window's control id is not known yet (the pause
menu's button that opens it is 0x0447060A, docs/08-ui.md).

## Fix
Not done: find the guide window's control id live (a UI tree dump while it is open) and add it to menu_check_task's
check. Cosmetic only (no gameplay effect).

## Verification
no-fail-run: not fixed yet.

## History
- 2026-11-21: opened (fix/l47-tribe live QA).
