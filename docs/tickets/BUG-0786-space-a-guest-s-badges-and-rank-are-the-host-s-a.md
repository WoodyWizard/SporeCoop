---
id: BUG-0786
title: Space: a guest's badges (and rank) are the host's again after a rejoin
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: Discord 2026-11-09 suggestions
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/space.md#space-kept-hold
related: BUG-0678, BUG-0679, BUG-0195
---

## Report
Discord suggestion 2026-11-09 "Badges should be shared with the party" (Space): the badges a guest earned in a
session are back to the host's after a rejoin.

## Root cause
Inferred, confidence medium:
* Observed (code): nothing in mods/coop handles badges (grep), no badge symbol in the .sym files; docs/re/space.md
  only mentions the badge panels and captions.
* Every player flies in a copy of the host's save and a rejoining guest loads the host's share save (as BUG-0678
  showed for the cargo hold). The badges and the rank live in the player's state saved with that world, so the
  guest's progress is replaced by the host's, and with the rank its rank rewards (tools).
* Same class as BUG-0678 / BUG-0679 (the kept cargo hold).

## Fix idea
RE first: the badge manager (object, table badge id -> level / progress, badge points, rank, their setters). Then the
space.keep_items scheme: the guest sends its badge table when it changes (a new K_* kind next to K_INV 77..79 in
space_trade.cpp), the host keeps it per player name (and the local record of BUG-0679) and gives it back when the
guest loads the host's world. Product question for the owner: personal badges (this fix) or a shared party progress
(the suggestion).

## How to test
New `coop_space_badges_rejoin.py`: the guest gets a badge (test command setting it through the game's setter), leaves
and joins again; assert the guest game's own badge field / rank after the load. Main: the host's badges: FAIL.

## History
- 2026-11-10: opened.
