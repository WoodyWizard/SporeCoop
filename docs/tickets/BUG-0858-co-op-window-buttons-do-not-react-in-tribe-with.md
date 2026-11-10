---
id: BUG-0858
title: Co-op window buttons do not react in Tribe with input_lock 0 (native registration or service dialog takes the clicks) (suspected)
status: open
kind: bug
stage: Tribe
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9018, BUG-9026
role: both
versions: 0.7.3.6 .. 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature: F9 opens, input_lock 0, a button receives no native click
docs:
related: BUG-0273
---

## Report
The fork saw the co-op window (F9) open in Tribe with the game's input lock at 0, while a button never got its
click; repeated clicks and reopening did not help. Later they found a native Spore registration modal and a delayed
online-service error dialog (caption 0x0451D381, close 0xFFFFFFF2) capturing input in their profiles.

## Root cause
* Observed (code): coop_ui.cpp locked_click_tick forwards a swallowed click only while input_lock() > 0 (BUG-0273);
  a modal that takes the input without raising the lock is not covered.
* Inferred: most likely a lab/profile effect (registration dialog); a player with such a dialog open would see dead
  co-op buttons. Suspected, confidence low.

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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
