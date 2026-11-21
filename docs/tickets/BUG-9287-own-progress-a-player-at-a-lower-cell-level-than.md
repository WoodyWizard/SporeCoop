---
id: BUG-9287
title: Own progress: a player at a lower Cell level than the host finds nothing to eat (the host populates the partner's area at the host's level): its DNA does not rise
status: open
kind: bug
stage: Cell
reported: 2026-11-20
source: owner 2026-11-20 (friends build test)
role: host, guest
versions: 0.7.15.94
fixed_in:
branch: feat/own-progress
commits:
tests: level_together
signature:
docs: docs/re/cell.md#cell-level-together
related: BUG-9265, BUG-9284, BUG-9285, BUG-9286
---

## Report
The owner (friends build 0.7.15.94, Cell): "sometimes eating meat or plants does not raise the DNA". Reproduced with
both games on 0.7.15.94 (lane A, a new Cell game, own progress on): the host ate to food 312 (level 4, world scale
15) while the guest stayed at 24 (level 1, scale 0.5): the guest's `coop.pool see` showed NOTHING on its screen, no
plant or meat it could eat, for minutes; its DNA could not rise. (A second observation, not reproduced again: right
after the new game 4 of 5 plants the host swam into vanished without points; in a second new game 6 of 6 gave points.)

## Root cause
✔ (code, docs/re/host-centric.md G7) The Cell pool is the host's game, populated at the HOST's level: PopulateTiles
takes the host's level base, also in the extra passes for the partners' areas (cellsync_tiles.cpp); GetScaleDifference
reads the avatar as the host's food level. With own progress (BUG-9265) each game's level follows its own food, so a
guest a level or more below the host swims among food and creatures of the host's levels: too big to eat, and big
creatures (giants). The shared total of 0.7.15 kept both on one level. With the friend on 0.7.15.5 (BUG-9284) the
friend's game took the host's food total over its own meals (its meals did not count).

## Fix
own_progress::clip_meal (cellsync_combat.cpp h_AddFood, own meals only): nobody grows a Cell level alone. An own meal
that would take this game's food over the next level line (every 100 food) while a partner is still on a lower level
gives only what fits; the rest is kept and added (cellsync::set_food_total, in the cell step) as soon as every partner
reached that level, or when the session / the setting ends. Notice: "Nobody grows alone: your cell waits at level N
until <partner> gets there too (your meals are kept: +K)". coop.progress: cap, bank, clipped, released. `coop.fix
cell.level_together off`: each level on its own (0.7.15.94). Not done (an alternative): populate each partner's area at
that partner's level (PopulateTiles with the partner's level base; the tile size of a lower level at the host's world
scale overflows the game's 511-entry buffer, BUG-0975, unless the partner's box is scaled: needs RE first).

## Verification
Built (WSL). tools/tests/coop_cell_level_together.py (level_together, twin level_together_off); in-game run on lane A:
see History.

## History
- 2026-11-20: opened.
- 2026-11-20: fix on feat/own-progress.
- 2026-11-20: lane A, 0.7.15.95: found and fixed in the game: both at 499 held each other for good (d20eef9d: the next level opens when every partner is at the line); a guest that left let the host take its kept meals and came back a level below with an empty screen (e4faf01a: partners away on record count). Then: host meal over the line stops at 599 with 51 kept, the guest crosses, the host gets 650: PASS; twin (cell.level_together off): the host crosses alone to 750: FAIL as expected. The owner's report after playing 0.7.15.94 ("I evolved faster than my friend and the food for him disappeared") is this bug.
