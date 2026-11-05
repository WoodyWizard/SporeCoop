---
id: BUG-0564
title: Tribe: after the guest dies, its map still shows villages that do not exist any more
status: open
kind: desync
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in:
branch: fix/w43-world-state
commits: a69ee62e
tests: tribe_goal (coop_wsync_tribe.py goal), tribe_bounce_world (coop_tribe_bounce_world.py, W2)
signature:
docs:
related: BUG-0483, BUG-0539
---

## Report
The guest (3 reports): after dying it still sees other villages on the map that should not exist.
Reports: SporeCoop-report-2026-11-04 190729 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s5734, s5771, s5911, s6244: the guest's quest is still "attack the Pink village's centre" while the host's moved on to the Lavender and then the Green village; where the host fights at a village with its hut, members and health bar, the guest's screen shows an empty plaza or another place.
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
Observed (host log): the host's game went Tribe -> Loading -> Tribe at s 5850, 6391 and 7320 (19:06:05, 19:15:06, 19:30:35: a game over and the game's reload of the last save; at 19:06 its members 25 -> 3) while the guest's game stayed in its world (no stage change in its log, only its mirrors 25 -> 3). Observed (lab): tribe_bounce_world out/lab/20261104-202045 FAIL: after the host's game over and reload the guest's game keeps the 3 NPC tribes the host's reloaded world does not have (emptied to 0 members by BUG-0483, but the tribe, its hut and map mark stay). Nothing re-aligns a guest's planet after the host's reload (W2).
* The guest's quest stayed "attack the Pink village" (screenshots s 5734 .. 6244): the host's reloaded world moved on.

* Observed (Ghidra, docs/re/tribe.md#tribe-wsync): the goal card is the Tribe Mission Manager's current
  cTribeToTribeMission (+0x74, its village +0x80); it follows the last village the player acted on
  (SetTargetTribe 0xCD3D70). Each game has its own: a guest's card never followed the host's.

## Fix
Goal part (W1): wsync row tribe_goal: the host's current mission's village (pol and position) and the goal progress
(cPlayer +0x10F0 / +0x10F4); a guest whose card is on another village calls the game's SetTargetTribe with its own
village outside the frame (the HUD card is swapped by the game), the floats are written. Switch `wsync.tribe_goal`.
The villages kept after the host's reload are W2's (fix/w43-realign).

## Verification
Lab: tribe_bounce_world (coop_tribe_bounce_world.py) FAIL on 0.7.6 code: out/lab/20261104-202045 (the guest keeps 3
villages the host's reloaded world has not). Package W2.

Lab (goal part): tribe_goal FAIL with `SPORE_FIX_OFF=wsync.tribe_goal` out/lab/20261104-205239 (the guest has no
mission, no card), PASS out/lab/20261104-205242 (the guest's mission and HUD card on the host's village within 1 s).
Class wsync_transitions FAIL out/lab/20261104-205648, PASS out/lab/20261104-205651.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W2), reproduced in the lab: tribe_bounce_world out/lab/20261104-202045 FAIL.
- 2026-11-04 (fix/w43-world-state, W1): goal part fixed (wsync row tribe_goal); the villages stay W2's.
