---
id: BUG-0632
title: Cell: a guest's kill is applied twice on the host (two breaks 0.6 s apart): a giant piece of meat
status: open
kind: bug
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-01
role: both
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0002, BUG-0420
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: cell_new_game, the guest kills NPC cells next to the host; on the host count the loot (meat) objects per
     guest kill and log the victim's dying / dead state between the two "applied" lines; repeat across a level-up. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
