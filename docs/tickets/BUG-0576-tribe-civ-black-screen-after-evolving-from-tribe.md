---
id: BUG-0576
title: Tribe -> Civ: black screen after evolving from Tribe into the city stage
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: host
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-transitions
commits: dc4084f5
tests: tribe_civ (tools/tests/coop_tribe_civ.py)
signature:
docs: docs/re/tribe.md#tribe-civ-transition
related: BUG-0313, BUG-0404, BUG-0585
---

## Report
The host: black screen after evolving into the city stage from Tribe.
Reports: SporeCoop-report-2026-11-04 201550 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s10034: the host's screen is black except the co-op window.
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (host log): the evolve vote for Tribe ended at 20:14:59 ("evolution starts"), then "the editor waits:
  the game is busy (paused)" and for 50 s no stage change (still "tribe(host)" at 20:15:35) until the mark:
  the mod waits for the game to unpause while the game's Tribe -> Civ change waits (black screen).
* The 0.7.3.6 log carried in BUG-0578 ends the same way ("the editor waits: the game is busy (paused)", then the
  player quit).

* Observed (Ghidra, GOG 2024): at the end of Tribe the game itself pauses before it asks for the editor: the stage's
  listener 0xCD1D60 takes the end of TRG2CVG_PreEditor, `cGameTimeManager::Pause(0x4BF38A6)`, the input lock +1,
  then `EditorRequest::Submit` of the City Hall editor 99E92F05 (and after it, the same way, the vehicle editor
  F670AA43 at the end of TRG2CVG_ShowCityHall). The pause and the lock are the editor's own: the editor lifts them.
* Observed (lab, lane 4, 0.7.6 code): the host's request starts the vote; at the vote's end the held request waited
  at the busy gate (BUG-0404, kEditorGate = cinematic | input lock | pause) for the game's own pause and lock: the
  editor opened only when the gate's limit ran out, 60 s with the input lock (lab), 300 s with the pause only (the
  players' log: `busy=paused` for 50 s, then the mark). Twice per Tribe -> Civ (City Hall, vehicle).

## Fix
`core.editor_own_pause` (mods/coop/coop.cpp): h_Submit keeps the busy bits the game had when it submitted the
request (kPaused, kInputLock); submit_tick does not wait for them (kPaused only while `GetPauseCount(0x4BF38A6)` > 0).
A cinematic, or a pause or lock that came later (the pause menu, a save box), still hold the editor as before.
`coop.fix core.editor_own_pause off`: the old gate (0.7.6).

## Verification
Test `tribe_civ` (coop_tribe_civ.py, tribe fixture): the goal bar filled (`coop.tribe advance`), then the players'
way: advance button, stage card, captions, the guest's "I'm ready" on both votes, the City Hall and vehicle editors
(3 parts and a name each); checks the host's game is in its editor at most 15 s after each vote and both games
reach Civ.
* FAIL (SPORE_FIX_OFF=core.editor_own_pause): out/lab/20261104-224338 (lane 5): editors 60.7 s and 61.3 s after
  the votes.
* PASS: out/lab/20261104-224318 (lane 4): 0.0 s and 0.7 s, both games in Civ, 0 faults; out/lab/20261104-224930
  (lane 4) and out/lab/20261104-224915 (lane 5, the BUG-0585 FAIL run): 0.0 s and 0.4 s.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (T1), observed in the host log.
- 2026-11-04 (fix/w43-transitions, T1): reproduced the players' way, root cause found, fixed (core.editor_own_pause).
- 2026-11-05: released in 0.7.7.
