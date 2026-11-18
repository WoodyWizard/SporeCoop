---
id: BUG-9101
title: Autosave: confirming the saved box leaves the virtual cursor on its OK button, and the player loses mouse control
status: fixed
kind: bug
stage: all
reported: 2026-11-09
source: player report (Lineages 40-dev, Cell, host)
role: host
versions: 0.7.3.6-lineages36-dev to 0.7.3.6-lineages40-dev
fixed_in: 0.7.3.6-lineages41-dev
branch: fix/fov-stuck-wide-lens
commits: 39cdca69
tests: QA `cursor` after `coop.save`, fix on and off (coop.fix world.saved_box_cursor)
signature: `cursor` answers virtual=1 for about 5 s after "the game saved box is up"
docs: docs/06-testing.md
related: BUG-9057, BUG-0177
---

## Report

"When the game autosaves, I lose control of my character for a brief moment
(in cell stage)."

## Findings

- The autosave posts the native save (world.cpp `save_game`). Spore saves on
  the main thread: a 0.6-1.0 s hitch in Cell and Creature logs (`perf: hitch
  ... saving`), 0.8 s in Tribe (06-testing). This part is native.
- Spore then shows the "game saved" box and pauses the simulation. The mod
  confirms it (BUG-9057) with SporeKit `move` + `click`. These set the virtual
  cursor; the hooked `GetCursorPos` answered the OK button's position until
  5 s without use. In Cell the avatar steers to the cursor, so the player had
  no mouse control for about 5 s. The same path runs in every stage
  (Space skips saves on a planet).
- The box was looked for every 200 ms while the simulation stood paused.

## Fix

- SporeKit `input_click(..., release_cursor)` / `click <x> <y> [button]
  release` hands GetCursorPos back to the real mouse right after the button-up.
  Older SporeKit ignores the extra word (a plain left click).
- The mod confirms with `click x y left release` and looks for the box every
  50 ms until it has appeared (`coop.fix world.saved_box_cursor`, off = before).
- SporeKit `cursor` reports the virtual cursor (tests).
- The native save hitch remains.

## Verification

QA host alone, Creature (Samuth), 5 FPS, muted; `coop.save`, then `cursor`
every 0.7 s:
- fix off: virtual=1 from 2.3 s to 8.5 s after the save (7 s).
- fix on: virtual=1 from 2.3 s to 3.8 s, i.e. only during the 6-frame click
  (1.2 s at 5 FPS, about 0.04 s at 144 FPS), then the real mouse.
- Box up 1813 / 1953 ms after the save (5 FPS). `exc` 0 faults.
Not covered: a real Cell session with a moving mouse; Tribe/Civ/Space saves.

## History
- 2026-11-09: reported by the player; fixed on fix/fov-stuck-wide-lens.
- 2026-11-09: released in 0.7.3.6-lineages41-dev.
