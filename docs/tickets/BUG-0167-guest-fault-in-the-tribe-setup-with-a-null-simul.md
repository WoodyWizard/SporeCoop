---
id: BUG-0167
title: Guest: fault in the tribe setup with a null simulator object (seen once)
status: fixed
kind: crash
stage: Tribe
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.3.0
branch: (earlier) d605e96
commits: d605e96
tests: tribe_play, chaos_tribe, tribe_coop
signature:
docs: docs/re/space.md
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `exception 0xC0000005 at 0x00B12D32` (data 0x2B4) on a guest of a 4-player tribe churn
  (`coop_multi.py`, 2026-10-23): the tribe setup 0xCD78E0, one step after a load, calls 0xB12CE0 on
  `[0x167FAAC]` (getter 0xB3D350), which was null (it is null in the galaxy menu). Not reproduced on
  2 players (`coop_rejoin_loop.py` plain, `--race`, `--reshare`, `--menu`: 0 of about 250 rounds).
  Since 2026-10-23 a load leaves a stage only when it has been playable for 3 s (`world.cpp`
  `kStageSettle`), which should avoid a quit during the setup; if it comes back, the fault log now
  shows the call chain (`exc`: "calls:"). docs/re/space.md 8.3.
* 2026-10-23: it came back once more (4-player tribe, `coop_multi.py`). Since then `gamefixes.cpp`
  skips `SimCamera_LookAt` and `SimCamera_Update` while the camera object is null (`coop.status`:
  "tribe camera skipped N"); the stage setup goes on without pointing that camera.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Mitigated on 2026-10-23 by commit d605e96 (before this ticket was backfilled): `gamefixes.cpp`
hooks `SimCamera_LookAt` and `SimCamera_Update` and skips them while the camera object is null
(`coop.status`: "tribe camera skipped N"). Both symbols are ported to GOG 2024
(`overrides_dc04aee5.sym`, verified). Nothing more changed for this ticket.

## Verification
Written argument (the race needs a 4-player churn and was seen twice, never on 2 players): the
fault was a call with a null `this` at that one site, and the guard returns before the call when it
is null. The guard is on in every tribe run on GOG 2024 (`game fixes: tribe camera guard on` in the
game logs of lab runs 20261027-183142, 20261027-185915); no such fault in the tribe runs of the lab
since (4 tribe_play + 4 chaos_tribe on main, 20261027-183142).

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-27: closed on fix/w1-tribe-civ as fixed by the camera guard of d605e96 (checked: guard on, GOG 2024 symbols verified).
- 2026-10-28: released in 0.7.3.0.
