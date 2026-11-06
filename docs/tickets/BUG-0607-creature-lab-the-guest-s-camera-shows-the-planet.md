---
id: BUG-0607
title: Creature (lab): the guest's camera shows the planet from space after the join (the intro cinematic 8915F9BB ended by the mod in a stage)
status: open
kind: bug
stage: Creature
reported: 2026-11-06
source: lab, fix/w45-creature-reports (creature_ufo_mirror shots)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0626
---

## Report
Seen in the lab (creature fixture, lane 6) while testing BUG-0558: two of the guest's screenshots taken 0.5-5 s
after its join (out/lab/20261105-230837's run family: 20261105-231804, 20261105-233051, out/lane6/shots/ufo_mirror_guest)
show the planet from space (the planet in the middle of a star field, the Creature HUD on), the others at the same
moment the creature at its nest. The guest's log of those runs: `world: cinematic 8915F9BB still playing in a stage:
ended it`; in the runs with a normal view: `... still playing at the load: ended it`.

## Root cause
Not analysed. Inferred: the fixture's intro cinematic (a zoom from space onto the planet) is ended by the mod
(world.cpp) while it plays in the stage; the camera may stay at the cinematic's first shot. Not seen in the host's
game, not checked whether it recovers by itself (the later shots of 20261105-233338 were normal). Not related to the UFO
mirror (seen also before its first ship).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-creature-reports): opened, not analysed.
- 2026-11-06 (fix/w45-int3): likely the same intro as BUG-0626 (the galaxy menu intro started in a stage after a quick load and ended there); world.intro_at_once no longer starts it outside the menu. Not re-tested on the Creature guest.
