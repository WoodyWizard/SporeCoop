---
id: BUG-0804
title: The mod's click on the game saved box keeps the virtual cursor on the OK button for 5 s: the player loses the mouse after every save
status: fixed
kind: bug
stage: all
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 BUG-9101 (commit 39cdca69, lineages41-dev)
role: host guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-saves
commits: cf952855
tests: saved_box_cursor
signature: SporeKit GetCursorPos answers the OK button's point for ~5 s after "the game saved box is up"
docs: docs/re/creature.md#creature-saved-box-kinds, docs/06-testing.md#testing-saves-w48
related: BUG-0599, BUG-0551
---

## Report
Fork player report (Cell, host): "When the game autosaves, I lose control of my character for a brief moment".
In our build the same happens after every save the mod makes (a guest joining, a world share, an evolution
save): for about 5 s the game reads the mouse at the saved box's OK button. In Cell the cell steers towards
that point; in other stages hover and camera ignore the real mouse.

## Root cause
* Observed in our code (confidence high, not measured by us):
  * `mods/coop/world.cpp:384` box_task confirms the box with the console command `click x y`.
  * `sporekit/input.cpp:136` input_click calls `vcursor_set` (input_move) and again at the button-down; nothing
    releases it after the button-up.
  * `sporekit/focus.cpp:182` vcursor_tick drops the virtual cursor only after 5000 ms without use, and
    `focus.cpp:39` h_GetCursorPos answers the virtual point while it is active.
* The fork measured it: fix off, `cursor` showed virtual=1 for 7 s after `coop.save` (at 5 fps); fix on, only
  during the 6-frame click.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages BUG-9101, commit 39cdca69); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* SporeKit: `input_click(x, y, button, bool release_cursor = false)`; in the button-up lambda (frame 6) call a new
  `vcursor_release()` (sets `g_vcursor_active = false` unless a button is held by `vcursor_hold`). The console
  `click <x> <y> [left|right|middle] [release]` passes the last word `release`; older SporeKit ignores it.
  Tests keep the old behaviour (no `release`).
* SporeKit: new console command `cursor` -> `virtual=<0|1> x=.. y=.. last_ms=..` (vcursor_state) for tests.
* Mod: box_task uses `click %d %d left release`; box_tick looks for the box every 50 ms (instead of 200) until it
  was first seen. Switch `world.saved_box_cursor` (off = before).
* The native save hitch (0.6 to 1.0 s) stays; it is the game's.

## How to test
Lab, one instance, Creature or Cell: `coop.save`, then the new `cursor` command every 0.5 s for 10 s. FAIL before:
virtual=1 for ~5 s after the box was confirmed. PASS after: virtual=1 only during the click frames, then 0.
Switch off: the old 5 s window returns. Also a real-mouse check in Cell (the cell follows the real pointer at
once after the box closes).

## Fix
SporeKit: `click <x> <y> [left|right|middle] [release]`: with `release` the virtual cursor is dropped 2 frames after
the button-up (vcursor_release, not while a button is held); new console command `cursor` (what the hooked
GetCursorPos gives the game). The mod's click on the saved box's OK uses `release`. Switch
`coop.fix world.saved_box_cursor off` = the plain click (5 s).

## Verification
`saved_box_cursor` (coop_saved_box.py --case cursor): `cursor` every 0.2 s for 7 s after the box closed.
* FAIL: out/lab/20261112-014119 (`SPORE_FIX_OFF=world.saved_box_cursor`, lane 3): the game read the OK point (980, 610)
  until 4.8 s after the box closed.
* PASS: out/lab/20261112-014220-2, out/lab/20261112-024053 (lane 3): never after the box closed (0.0 s).

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (code audit, not reproduced by us).
- 2026-11-12: fixed on fix/w48-saves (world.saved_box_cursor), FAIL 20261112-014119, PASS 20261112-014220-2.
