---
id: BUG-1063
title: UI: in Space the player list says 'other world' for a partner at another star, or for everyone while the own ship is in the galaxy view or respawning
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: fix/w49-space (S1) observation, checked by code on fix/w49-space2
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
