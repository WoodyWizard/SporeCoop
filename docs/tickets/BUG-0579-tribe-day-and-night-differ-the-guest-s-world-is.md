---
id: BUG-0579
title: Tribe: day and night differ: the guest's world is at night while the host's is in daylight
status: open
kind: desync
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build), screenshots
role: guest
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
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

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in a plain session (the lab tod follows). Not tried: the host's game over and reload (tribe_bounce_world does not compare the time of day). Package W1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; not reproduced in a plain session; open (W1) for the reload case.
