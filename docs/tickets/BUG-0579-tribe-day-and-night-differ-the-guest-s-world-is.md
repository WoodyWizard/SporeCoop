---
id: BUG-0579
title: Tribe: day and night differ: the guest's world is at night while the host's is in daylight
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build), screenshots
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-world-state
commits: a69ee62e
tests: wsync_transitions (coop_wsync_transitions.py)
signature:
docs:
related: BUG-0569
---

## Report
Seen only on the screenshots of the 2026-11-04 reports (SporeCoop-report-2026-11-04 190900, 191227, 191438, guest and host at the same session second): s6054, s6244, s6375, s6403: the guest's sky and ground are dark (night) while the host's screen at the same second is in daylight; at s5911 and s6037 both were in daylight. The players did not mention it.
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (code): the host sends its time of day every second (MSG_WORLD_TIME, coop.cpp), a guest applies it
  while it has an avatar (game::set_time_of_day: the planet context's orientation + TimeOfDay_Apply).
* Observed (lab, tribe fixture on 0.7.6 code): TimeOfDay_GetObject()+0x10 on both games, read twice minutes apart:
  the guest's quaternion equals the host's within the 1 s send (e.g. (-0.093 0.311 -0.945 0.036) / (-0.092 0.312
  -0.945 0.021)). The night on the guest came after the host's reloads (s 5850, 6391): inferred, the host's load
  changes its time of day and its sky in a way the guest's quaternion does not (another object, or the preset).

* Observed (code + lab 20261104-205648, every fix off): MSG_WORLD_TIME was applied only with `game::avatar()`, which
  is null outside Creature and adventures, so a Tribe or Civ guest never took the host's sky; each game's sky ran
  from the save's time of its own load: 0.097 rad apart in a plain session, 0.089 after the host's game over and
  reload. Step 0's "equal within the 1 s send" was two games loaded close together.

## Fix
wsync rows tod (the planet context's orientation, applied with SetOrientation + TimeOfDay_Apply while the guest has
its own entity, in Creature, Tribe and Civ) and day (cTimeOfDay +0x24, SetDayDuration); a guest with the row on
ignores MSG_WORLD_TIME (kept for older guests). Switch `wsync.tod`.

## Verification
Not reproduced in a plain session (the lab tod follows). Not tried: the host's game over and reload (tribe_bounce_world does not compare the time of day). Package W1.

Lab: wsync_transitions FAIL with `SPORE_FIX_OFF=wsync.tod,...` out/lab/20261104-205648 (0.097 rad before the
transitions, 0.089 after the host's reload, 0.054 at the end), PASS out/lab/20261104-205651 (within 0.05 rad at
every step).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; not reproduced in a plain session; open (W1) for the reload case.
- 2026-11-04 (fix/w43-world-state, W1): fixed; see Fix and Verification.
- 2026-11-04 (fix/w43-detector2, D0 detector): `All:state:time_of_day` in 16 % of all comparisons, long_tribe only (lab 20261104-203017): from about 10 min into the session the host's sky stood still (q (-0.08 -0.08 -0.06 0.99) in consecutive samples) while the guest's moved, 25 deg apart growing to 145 deg.
- 2026-11-05: released in 0.7.7.
