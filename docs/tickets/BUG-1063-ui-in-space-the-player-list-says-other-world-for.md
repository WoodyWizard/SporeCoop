---
id: BUG-1063
title: UI: in Space the player list says 'other world' for a partner at another star, or for everyone while the own ship is in the galaxy view or respawning
status: fixed
kind: bug
stage: Space
reported: 2026-11-13
source: fix/w49-space (S1) observation, checked by code on fix/w49-space2
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-ui
commits: 8bc5ebf9, 66f43dd7
tests: space_other_world
signature:
docs: docs/06-testing.md#testing-p2-ui
related:
switch: core.ui_other_world
---

## Report
Seen by the S1 agent (fix/w49-space, 0.7.14): after the guest's ship was destroyed and respawned, the guest's co-op
window showed "Alice [Space, other world]" for a moment, although both played the host's galaxy. Cosmetic: the label
reads as "Alice plays another save".

## Root cause
Observed in code (fix/w49-space2, coop.cpp player rows ~6330 and the status line ~4616), not run:
* the row adds ", other world" when the partner's mode is the own mode and `same_place(ls)` is false;
* `same_place` needs `g_world != 0` and the same world; in Space `g_world` is `modes::current_place` = the Space
  adapter's place (modes.cpp space_place): PlanetID | 0x40000000 on a planet, StarID | 0x80000000 in a system, **0 in
  the galaxy view** (docs/07-modes.md: "In space the place is a star or planet").
* So in Space the label says "other world" for every partner at another star or planet, and for all partners while
  the own ship is in the galaxy view, in transit or respawning (place 0). The respawn moved the guest's ship home
  (another place, or 0 on the way): the moment S1 saw.
Not a data bug (nothing is applied by this label); the same `same_place` gates what is applied in Space by design.

## Fix idea
In Space say where the partner is ("Space, other star" / nothing while the own place is 0), or compare the Space world
(the host's world tag / spaceext::world_epoch shared) for the "other world" word. How to test: two guests at different
stars, `coop.uistate` player rows; galaxy view of the guest.

## Fix
coop.cpp other_world_label() for the co-op window's rows (ui_players) and the status block: outside Space as
before (!same_place); in Space "other world" only for the host's row on a guest that plays another Space save
(guest_space_other, BUG-0709 / BUG-0933); the host and guest-to-guest rows say nothing there (the save of another
guest is not known). Switch `coop.fix core.ui_other_world off` (!same_place in Space, 0.7.13).

## Verification
coop_space_other_world.py (suite `space_other_world`): both co-op windows' rows at the home star and with the
guest in the galaxy view (its place 0).
* FAIL (SPORE_FIX_OFF=core.ui_other_world): out/lab/20261113-032606 (both rows "Space, other world" in the galaxy view)
* PASS: out/lab/20261113-032515, out/lab/20261113-032833 (screenshot: "Space" on both rows)

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-ui (0.7.14 P2).
- 2026-11-14: released in 0.7.14.
