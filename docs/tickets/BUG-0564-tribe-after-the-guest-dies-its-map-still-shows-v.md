---
id: BUG-0564
title: Tribe: after the guest dies, its map still shows villages that do not exist any more
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-world-state, fix/w43-realign
commits: a69ee62e, 736eaef5, cc62df1a
tests: tribe_goal (coop_wsync_tribe.py goal), tribe_bounce_world (coop_tribe_bounce_world.py), realign_tribe (coop_realign_planet.py --fixture tribe --case gameover)
signature:
docs: docs/re/tribe.md#tribe-realign
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
2026-11-04 (fix/w43-realign): observed (lab): after the host's reload the host's world has 2 tribes, the guest's 5; tribe.cpp's member sync maps a guest tribe to a host tribe within 40 m only and BUG-0483 empties an unmapped one of its members, nothing removed the tribe object, its hut, tools, food mat. Observed: the host's whole tribe list reaches the guest (MSG_TRIBE_STATE), so the guest had the information; it never acted on a tribe as a whole.

* Observed (Ghidra, docs/re/tribe.md#tribe-wsync): the goal card is the Tribe Mission Manager's current
  cTribeToTribeMission (+0x74, its village +0x80); it follows the last village the player acted on
  (SetTargetTribe 0xCD3D70). Each game has its own: a guest's card never followed the host's.

## Fix
Goal part (W1): wsync row tribe_goal: the host's current mission's village (pol and position) and the goal progress
(cPlayer +0x10F0 / +0x10F4); a guest whose card is on another village calls the game's SetTargetTribe with its own
village outside the frame (the HUD card is swapped by the game), the floats are written. Switch `wsync.tribe_goal`.

Villages part (W2): whole-planet re-alignment (mods/coop/realign.cpp, realign_rule.h): the host sends its planet's villages, huts, tools, food mats, nests, cities every 2 s and at once after its own load / game over reload or a guest's (re)arrival; a guest matches them by position (the player village only with the host's) and a village of its own without a host partner on two complete lists in a row goes through the game's own removal, cGameNounManager::DestroyInstance of the tribe (tribe::remove_village: never the player tribe, never one the member sync maps, never during a cinematic; its members first): its hut, tools and food mat go with it. Decision: re-align in place, no reload of the host's world on the guest. A guest's own creature attack event in Tribe faulted after such a removal: it does not start on a guest in Tribe (drama.cpp, `tribe.guest_attacks`). Switches: `coop.fix realign.villages off` (the village stays, emptied: 0.7.6), `coop.fix tribe.guest_attacks off`.

## Verification
tribe_bounce_world: FAIL with `SPORE_FIX_OFF=realign.villages` out/lab/20261104-204705 (the guest keeps the 3 villages at (446 -150 185), (438 181 188), (502 -12 67)), also on 0.7.6 out/lab/20261104-202045 (fix/w43-replay); PASS out/lab/20261104-211648. realign_tribe (whole planet: villages, huts, food mats, tools, nests after the game over): FAIL with `SPORE_FIX_OFF=realign.villages,realign.buildings` out/lab/20261104-212017, PASS out/lab/20261104-212353. Before tribe.guest_attacks a PASS run had a guest fault 15 s after the removal: out/lab/20261104-211318. Not verified: the planet map's marks (inferred from the noun list), the quest's target (W1's goal).

Lab (goal part): tribe_goal FAIL with `SPORE_FIX_OFF=wsync.tribe_goal` out/lab/20261104-205239 (the guest has no
mission, no card), PASS out/lab/20261104-205242 (the guest's mission and HUD card on the host's village within 1 s).
Class wsync_transitions FAIL out/lab/20261104-205648, PASS out/lab/20261104-205651.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W2), reproduced in the lab: tribe_bounce_world out/lab/20261104-202045 FAIL.
- 2026-11-04 (fix/w43-world-state, W1): goal part fixed (wsync row tribe_goal); the villages stay W2's.
- 2026-11-04 (fix/w43-realign): fixed by the whole-planet re-alignment (realign.villages); FAIL / PASS runs above.
- 2026-11-04 (fix/w43-detector2, D0 detector): `Tribe:state:goal` in 28 % (the host's tribe mission manager has a current mission for a tribe, the guest's none) and `Tribe:planet:village` in 7 % (the guest's planet has 3 villages more than the host's: e.g. at (446 -150 185), where the host's tribe object keeps a centre but no placed position) of the Tribe comparisons, labs 20261104-203017 / 20261104-210208.
- 2026-11-05: released in 0.7.7.
