---
id: BUG-0858
title: Co-op window buttons do not react in Tribe with input_lock 0 (native registration or service dialog takes the clicks) (suspected)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9018, BUG-9026
role: both
versions: 0.7.3.6 .. 0.7.11.1
fixed_in: 0.7.4
branch: fix/w48-tribe-civ
commits: 1c880451
tests: ui_modal_tribe
signature: F9 opens, input_lock 0, a button receives no native click
docs:
related: BUG-0273 BUG-0482
---

## Report
The fork saw the co-op window (F9) open in Tribe with the game's input lock at 0, while a button never got its
click; repeated clicks and reopening did not help. Later they found a native Spore registration modal and a delayed
online-service error dialog (caption 0x0451D381, close 0xFFFFFFF2) capturing input in their profiles.

## Root cause
Reproduced in Tribe with the input lock at 0 and a modal box of the game up; already fixed by BUG-0482's follow-up.
* Observed (lab 20261112-091300, `core.ui_modal_edit` off): in Tribe the host's "game saved" box held up
  (`coop.save hold`; the window manager's modal window cWindowManager +0x82C, `modal=1`), `input_lock=0`; F9 opens
  the co-op window, a real click on its Settings button does nothing (the page stays the lobby): the fork's
  symptom. Without the box the same click works.
* Cause: the game's UI gives no click to other windows while a modal window is up, and 0.7.3.6's forwarding of a
  swallowed click (BUG-0273, `core.ui_locked_clicks`) looked at the input lock only. The fork's base is our 0.7.3.6;
  since 0.7.4 `clicks_locked()` (coop_ui.cpp) also counts any modal window of the game that is not ours
  (`core.ui_modal_edit`, BUG-0482), so the click under the box is given to the button. The fork's registration and
  service-error dialogs are modal windows of the same window manager (inferred from "modal" in its report; not
  opened here).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9018 switch core.ui_tribal_clicks, BUG-9026); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* coop_ui.cpp locked_click_tick: the forwarding path is also allowed when input_lock() <= 0 in Tribe with the fork's
  village mode; it waits for the native delivery and uses the existing native-delivery suppression before running the
  missed button's action outside the frame. Switch `core.ui_tribal_clicks`.
* BUG-9026: the Tribal camera test driver closes the specifically identified service-error dialog (caption 0x0451D381,
  close button 0xFFFFFFF2) with visibility checks before and after; UI tests check for registration modals instead of
  trusting input_lock.
* For us: rather detect any visible native modal over the window and say so, or forward like BUG-0273.

## How to test
Lab: open the registration / service-error dialog in a test profile (or a test command that raises a modal without
the input lock), open F9, click a button. Assert the button's action runs (or a clear message is shown). FAIL on main,
PASS with the fix. Also make the tests' preflight check for these dialogs.

## Fix
Already in 0.7.4: `core.ui_modal_edit` (BUG-0482 follow-up, coop_ui.cpp clicks_locked) gives a click on the co-op window to its button while any modal window of the game is up, whatever the input lock. This release adds the Tribe test that fails without it.

## Verification
`coop_ui_modal_tribe.py` (suite `ui_modal_tribe`, tribe fixture, ~1 min): in Tribe the host's "game saved" box held
up (input lock 0, modal 1), F9, a real click on Settings: the page must change; Back; then the same without the box.
* FAIL with `SPORE_FIX_OFF=core.ui_modal_edit` (the 0.7.3.6 behaviour): lab 20261112-091300 (page stays the lobby).
* PASS: lab 20261112-091412, 20261112-091646-2.

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12 (fix/w48-tribe-civ): reproduced with core.ui_modal_edit off; fixed since 0.7.4 by BUG-0482's modal rule (test ui_modal_tribe).
