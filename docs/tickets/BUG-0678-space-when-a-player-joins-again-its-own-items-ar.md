---
id: BUG-0678
title: Space: when a player joins again, its own items are gone and it has the host's items instead
status: fixed
kind: bug
stage: space
reported: 2026-11-07
source: player report 2026-11-07 (Discord bug-reports post 'Items are not saved for players.', tag Space, text only, version not given)
role: 
versions: unknown (0.7.9 is current)
fixed_in: unreleased
branch: fix/w46-space-items
commits: 767118a2, 13dccfa9, 08ff13df
tests: space_items_rejoin
signature:
docs: docs/re/space.md#space-kept-hold, docs/06-testing.md#testing-space-kept-hold, docs/known-issues.md#issue-space-kept-hold-limits
related: BUG-0195, BUG-0058, BUG-0679
---

## Report
Player report (Discord #bug-reports, new post, tag Space, 2026-11-07), verbatim:
"When players log back in, their items disappear and the host's items appear - I assume this is due to item
synchronization being disabled."
No logs, no version. Reading: in Space, a guest that leaves and joins again (or reloads) gets the host's world save;
its own inventory (cargo, collectables, tools, maybe spice) is replaced by the host's.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lab 20261107-122430, fix off; code): every player flies in a copy of the host's save, and a joining
guest always loads the host's share save (world.cpp `share_with`). The hold is the game's `cPlayerInventory`
(`cSimulatorSpaceGame +0x40`, GOG 2024 vtable 0x1494D70), saved with the world, and the mod never kept or
synchronised it: after the rejoin the guest's game had the host's spice (1CA01560 x5), the host's bomb charges (3
instead of its 9), and lacked its own spice (1CA01562 x7) and its interstellar drive (2569FAE8). Money is not
affected (one shared budget). The first join gives the guest the save's hold too: by design (the guest's start).
Inferred: the player's "item synchronization disabled" is this; no item sync setting exists.

## Fix
space_trade.cpp, switch `coop.fix space.keep_items` (off = 0.7.9: the rejoined guest has the host's hold).
The guest sends its hold (K_INV 77: type, key, count, charges per item; the cargo slot count) whenever it
changes, from the first world it loaded from the host on. The host keeps the last one per player name for its
Space world (memory only, like BUG-0195's colonies). A guest's first hold of a newly loaded world is the host's
copy: the host answers with the kept one (K_INV_KEPT 78, resent after 10 s until K_INV_DONE 79), or with "nothing
kept" on a first join. The guest makes its game's hold the kept one: the items it does not own leave (TakeItem
0xFF40B0, whole or the surplus), bought cargo slots come back (AddCargoSlots 0xFF39E0, never fewer), tools by
AddItemByKey 0xFF4490 with their charges (AddAmmo / ConsumeAmmo), spice by CreateTradingObject 0x103A490 +
AddItem, other cargo stacks by their count. Owed trade refunds (BUG-0058) wait until that is done (at most 20 s),
so they are not paid into a hold about to be replaced. Limits: BUG-0679.

## Verification
`coop_space_items_rejoin.py` (suite `space_items_rejoin`), lane 2:
* FAIL with `SPORE_FIX_OFF=space.keep_items`: lab 20261107-122430 (the guest had the host's hold).
* PASS: lab 20261107-122320, 20261107-122629 (before the refund gate commit), 20261107-122835 (final code, with 12 Space regressions: 13/13).

## History
- 2026-11-07: opened.
- 2026-11-07: reproduced and fixed on fix/w46-space-items (space.keep_items); limits in BUG-0679.
