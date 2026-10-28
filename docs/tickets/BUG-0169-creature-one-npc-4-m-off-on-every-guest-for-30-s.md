---
id: BUG-0169
title: Creature: one NPC 4 m off on every guest for 30+ s (seen once)
status: fixed
kind: desync
stage: Creature
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-creature
commits: 9c499bf
tests: unit (npc_tests: proxy driving)
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `out/join_desync.py`-style probe, 4 players, 2026-10-23 (after the menu-load fix): NPC n11 stayed
  4 m from the host's on Bob, Carol and Dave alike from 8 s to the end (36 s); the 19 others < 1 m.
  The same offset on every guest points at the host side or at a spot the proxy cannot stand on
  (pushed out by collision after each stuck-teleport of `drive::step`). Not seen again in 5 min of
  watching nor in 4 more sessions. Hunt: `coop.npc list` on host and guests for that id (err, want).

## Root cause
See the report (observed vs inferred as marked there).

## Fix
The candidate cause of wave 2 (inferred, not observed): `drive::step` (driver.h) teleported only
above 8 m, or after 1.5 s when the host's NPC reported less than 0.3 m/s. An NPC of the host that
pushes against something (a speed reported, the position fixed) kept the proxy walking to a spot
it could not reach on every guest, for as long as it pushed: the same offset on every guest.

Fix (fix/w3-creature): the frame's decision of `drive::step` is a pure function now
(`mods/coop/drive_plan.h`). For NPC proxies (`npc.cpp`, `stall` on) a proxy that stays more than
2.5 m off for 3 s without getting 0.5 m closer is teleported whatever speed the host reports; after
such a teleport it waits 10 s before another one (a spot it cannot stand on: pushed out by
collision again). Player puppets and tribe mirrors drive as before. `coop.npc stallfix off` keeps
the old driving.

## Verification
Not reproduced in the game (no NPC pushing against a rock could be set up on the fixture in the
time; the 4-player probe was not run). Unit test `tests/npc_tests.cpp` `test_drive` (make test), the
driving decision with a host NPC that reports 2 m/s and stands 4 m from where the proxy can get:

* without the fix (`stall_fix` false): 0 teleports in 40 s (the proxy stays 4 m off);
* with it: 1 teleport (after ~3 s); a spot it cannot stand on: 2-4 teleports in 40 s, not one per
  frame; a walking puppet that catches up is never teleported; the old stuck teleport still fires.

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w2-creature: not reproduced; a candidate cause from driver.h written down (see Fix). BUG-0044 (reused objects) ruled out as the cause: it shows another species or a corpse, not an offset.
- 2026-10-28: fix/w3-creature: the candidate cause fixed in driver.h (stall teleport for NPC proxies, drive_plan.h) with a unit test; not reproduced in the game.
- 2026-10-28: released in 0.7.3.0.
