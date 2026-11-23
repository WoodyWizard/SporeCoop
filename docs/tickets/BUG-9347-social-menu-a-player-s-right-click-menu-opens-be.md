---
id: BUG-9347
title: Social Menu: a player's right-click menu opens behind the Social panel (its top hidden)
status: fixed
kind: bug
stage: menu
reported: 2026-11-23
source: 
role: 
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 94508298
tests: tools/qa_win (manual, 2026-11-23): TITAN, QA 8 and QA 9 on the test galaxy, a right-click on a row of the Social Menu; coop.fix ui.menu_beside_social off/on
signature:
docs: docs/12-server-and-players.md#server-social-menu
related: BUG-9334
---

## Report
Seen while taking Social Menu pictures for the owner (2026-11-23): QA 8 (1280x960) with the Social Menu open, a
right-click on another player's row: the player menu opened at the pointer under the panel, its title and first
item hidden (only Whisper and Add Friend showed below the panel).

## Root cause
Observed: the menu opens at the pointer, which is on the panel. `coop.ui where` listed the Social panel in front of
the menu even after the menu's bring_to_front (both are main-window children flagged always-in-front); raising the
menu again on the panel's refresh did not change it (first fix attempt, not kept). With the switch off in a later run
the menu showed over the panel instead, covering its rows: either way the two overlap.

## Fix
coop_ui.cpp pm_apply: a menu asked at a point on the Social panel opens beside it (left of the panel, else right),
at the pointer's height. Switch `coop.fix ui.menu_beside_social off`: at the pointer as before.

## Verification
TITAN, QA 8 Explorer and QA 9 Builder on the test galaxy:
- FAIL (before the fix, the build of BUG-9346): the menu under the panel, its top hidden (two runs); with the switch
  off on the fixed build: the menu over the panel's rows (images/bug9347_off.png).
- PASS (on): "Builder: Invite to Party, Whisper, Add Friend" whole, left of the panel (images/bug9347_on.png); the
  leader's menu on a party member likewise; Add Friend and Invite worked from it.
no-fail-run: Windows QA by hand; the runs above are the switch off and on.

## History
- 2026-11-23: opened.
- 2026-11-23: fixed on feat/lineages-alpha.
