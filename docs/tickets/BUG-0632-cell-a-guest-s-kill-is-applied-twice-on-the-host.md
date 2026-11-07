---
id: BUG-0632
title: Cell: a guest's kill is applied twice on the host (two breaks 0.6 s apart): a giant piece of meat
status: fixed
kind: bug
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-01
role: both
versions: 0.7.8
fixed_in: unreleased
branch: fix/w46-cell-p0
commits: d7a0a2c5
tests: cell_kill_once (coop_cell_once.py --case kill)
signature:
docs: docs/re/cell.md#cell-once
related: BUG-0002, BUG-0420
switch: cell.kill_once
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/Sterben-01 (the host, mark at s 132.8): "A giant piece of
meat". The guest's screenshot at the same second (M0ng0r-01, s 133) shows a huge orange fleshy object at the left edge
(probably the meat; the host's own shot is covered by the co-op window).

## Root cause
Observed (the host's log): the guest's lethal break of the same cell applied twice 0.6 s apart, the third refused:
`[s 126.012] eat: from 1 kill c2566783835 (creature): applied`, `[s 126.614] ... applied`, `[s 127.219] ... refused
(gone or dying here)`. Over the session 9 of the 30 cells the guest killed were applied twice (44 kill messages): the
guest sends 2-3 `sent break` per kill while it keeps biting the mirror. The host's level went 2 -> 3 at s ~123.8, just
before.
Inferred (not verified): the second break on the host (cellsync.cpp h_Break, MSG_CELL_EAT ACT_BREAK) runs the death and
its loot again because the victim is not marked dying yet: two meat drops on one spot; or loot spawned across the
level-3 rescale at the old size.
Related: BUG-0002 (a chunk eaten by two counted twice, fixed 0.7.2.2); class BUG-0420 (hits / world events applied
without a once-only guard).

## Fix
`cell.kill_once` (cellsync.cpp; the shared once-only ledger `cell_logic::Once` of class BUG-0420,
docs/re/cell.md#cell-once): the guest sends a lethal bite (ACT_BREAK) or a kill (ACT_KILL) of a host index once
in 10 s (`kills_dup_sent` counts the ones it leaves out; the break effect plays once), and the host applies a kill
or break of an index once in 10 s ("refused (killed here already)", `kills_dup_refused`), whoever sends it.
`coop.fix cell.kill_once off`: 0.7.9.

## Verification
* Root cause observed (lab, `coop_cell_once.py --case kill`: the guest bites a creature to death with the game's
  Bite and bites on for 1.5 s, as a player whose cell stays on the body): the host's break plays its animation
  before the dying flag and the loot (event 5), so the guest's mirror still looks alive (hp 0, no dying flag in
  the state) and its game breaks it again with every bite; each one reached the host and broke the cell again.
* FAIL (`SPORE_FIX_OFF=cell.kill_once`): out/lab/20261106-215114, the host's game broke one creature 4 times
  (npc_deaths +4, kills_in 4) and the next 3 times; its retry 3 and 3.
* PASS: out/lab/20261106-215500 (#1, #2): every kill broken once on the host; the guest left out 10 repeated
  lethal bites.
* Area tests: out/lab/20261106-223316 (cell_meat, cell_gone, cell_sync, cell_levelups, cell_new_game OK); with
  SPORE_DETECTOR=20: out/lab/20261106-223318 (cell_kill_once, cell_eat_once OK, no detector row).

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-06: fixed in fix/w46-cell-p0 (cell.kill_once): the guest sends a kill once, the host applies it once; FAIL 20261106-215114, PASS 20261106-215500.
