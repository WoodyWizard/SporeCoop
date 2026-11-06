---
id: BUG-0636
title: Cell: the guest can eat the same plant or meat piece twice: its mirror is made again before the host's state drops it
status: open
kind: desync
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-03 (the guest's mark)
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0002, BUG-0088, BUG-0589, BUG-0420
---

## Report
Discord report 2026-11-06 (0.7.8), the guest's mark at s 593.4 (seen in the host's chat in discord-2026-11-06/Sterben-03):
"in cell stage I can sometimes eat the same big plant's part twice".

## Root cause
Observed (both logs): the guest sends "gone" for the same host index 2-4 times, 30-100 ms apart; the host applies the
first and answers "not here, no points" to the rest: guest `[s 581.883] sent gone c110625761 ... meal=450` and
`[s 581.917] sent gone c110625761 ... meal=451`; host `meal=450: applied`, `meal=451: (-) not here, no points`; the same
for c110494700, c108856376, c35652806. Over the session 193 "sent gone", 38 objects sent 2-4 times (12 plant, 26 meat).
Observed (code): the destroy hook (cellsync.cpp) erases the mirror from `g_host_of` / `g_mirrors` when the guest eats
it; no tombstone of recently eaten host indices. Until the host's state drops the index (about one round trip) the
guest's mirror pass makes it again and the guest's game can eat it again with the game's own food each time ("own cell
ate +3", then "sent food +3 (no host cell with it)", which the host applies).
Inferred: double food for the guest per repeated meal. Related: BUG-0002, BUG-0088, BUG-0589 (fixed); class BUG-0420.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: cell_new_game with an added delay (300 ms rtt, tools netem); count `sent gone` with a repeated host index
     and compare the food total with the meals. Fix idea: a tombstone of eaten host indices until the host's state
     drops them. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
