---
id: BUG-0687
title: Creature, 4 players: the host stops sending its state when a guest rejoins, its share saves for that guest are never written and it freezes about 30 s later
status: not-reproduced
kind: freeze
stage: creature
reported: 2026-11-07
source: lab
role: host
versions: 0.7.10
fixed_in:
branch: fix/w46-k7-findings
commits: 4ec6ff91, fe3800b6, a1d1cc7a
tests: campaign_tribe (seed 953406079), campaign_tribe slow, multi4_creature_slow, keepalive_stall
signature: host log ends with "world: no saved box and no file written 10013 ms after the save for player 1: saving again (try 2/3)"; FROZE (no frame for 10 s)
docs: docs/known-issues.md#issue-creature-rejoin-share-freeze, docs/06-testing.md#testing-k7-findings
related: BUG-0686
---

## Report
campaign_tribe on lab lane 3, rel/0.7.10 4c1d891f (lab 20261107-182231, run #2, campaign
out/lane3/campaign/20261107-183105 of the rel/0.7.10 worktree): "the campaign stopped in creature (wanted tribe)".
In Creature play the chaos step made Bob leave and join again (s 86 -> s 93 of the host log). From then on:
* the host's trace lines (every 10 s) stop after s 80; the guests' monitors see the host's state age grow from
  about s 86 ("partner 0 (Alice) ... hidden: state older than 5 s" from s 100, "link unhealthy, state 13.7 s old");
* "sending the world to Bob", "world: saving for the share of player 1" at s 93; no "game saved" box and no file
  10 s later: "saving again (try 2)" at s 103, "(try 3)" at s 114, a third post at s 115, then the host's log ends;
* the relay to the other guests stops at about s 121 (Bob's log: Carol and Dave "not responding" at s 126);
* the console times out at +246 s, the watchdog reports FROZE (no frame for 10 s) at +266 s.
The host still ran frames between s 86 and s 115 (DNA and save retry lines), so the main loop was alive while it
sent no state.

## Root cause
Not found; what the logs show (fix/w46-k7-findings, 2026-11-08):
* Observed: the host's frames stopped right after it handled Bob's leave (s 86.196): its last player state reached the
  guests at s 86.3 (Carol at s 90.009: Alice state_age=3.7 s) and its own trace lines, written at the end of on_frame,
  stop after s 80 (no s 90 line). on_frame has no early return (coop.cpp on_frame), so it was not called.
* Observed (code): every host line after s 86.3 (the rejoin at 93.4, the share save and its retries at 103.4 / 114.5,
  DNA and relationship lines driven by guest messages) can come from the keepalive: keepalive_task (coop.cpp) runs
  g_sess.update and world::tick through run_outside_frame, which the window procedure executes (sporekit/console.cpp
  pump_outside_tasks, focus.cpp h_WndProc) when no frame came for 0.5 s. The share save is a game message handled in
  the game's update: with no update no file and no box (world.cpp save_game), as logged.
* Inferred: the main thread sat in a nested message loop that was neither App::Canvas::Tick (SporeKit's game-loop
  fallback frame would have started after 2 s and logged `frames: Present has not reached SporeKit`) nor Present.
  What opened it is not in the log. The watchdog's dump 30 s later never reached the log (the log flushes each line);
  inferred: its SuspendThread + VirtualQuery stack walk can hang under Wine (the MinHook freeze class), which also fits
  the full stop at about s 121.
* Not a 0.7.10 regression by the code (nothing after e7d1734c touches the leave or share paths); v0.7.9 passed the
  seed 2/2 (fix/w46-advance-reg).

## Fix
No fix (cause not found). Diagnostics so the next occurrence names the loop: the keepalive logs once per stall of 3 s
the main thread's own call chain (return addresses into the game's code and user32 frames, read on the main thread
itself, no suspend) and its windows (class, title, visible: a #32770 is a dialog): `keepalive: no frame for N s, the
main thread runs a message loop (BUG-0687): chain ...` and `keepalive: the main thread's windows ...`. Test hook
`coop.stalltest <s>` (an outside-frame command: the main thread runs its own message loop for s seconds).

## Verification
no-fail-run: not reproducible (the report's seed passed 4 more times, below); the diagnostics' test keepalive_stall PASSES
(lab 20261108-012143, lane 9: both lines in the host's log, frames go on after the 6 s stall).
Dedicated attempts on fix/w46-k7-findings (lane 10, 4 players, the report's chaos step Bob leaves and joins again):
- campaign_tribe CAMPAIGN_SEED=953406079: lab 20261108-010321 OK (9 min, rejoins 2, no stall line, no FROZE).
- the same at 20 fps over the players' network (SPORE_FPS=20 SPORE_NET=players): lab 20261108-011356 OK (rejoins 2).
- multi4_creature_slow (4 players, 6 min of chaos-bot churn with leaves and rejoins, 20 fps, players' network): lab
  20261108-012301: no freeze, no stall line; failed only on a monitor drawn gate after a 1.2 s state gap (BUG-0711, the
  K4 class).
- Before (fix/w46-advance-reg, 2026-11-07): the seed passed 2/2 on v0.7.9 (lane 2) and 2/2 on rel/0.7.10 (lane 3).

## History
- 2026-11-07: opened (fix/w46-advance-reg): one lab run, seen once.
- 2026-11-07: the replay seed 953406079 (creature -> tribe, 2 min) passed 2/2 on v0.7.9 (lane 2) and 2/2 on rel/0.7.10 at the BUG-0686 merge (lane 3): not reproduced, not shown to be a 0.7.10 regression. Planned for 0.7.11 (K7).
- 2026-11-08 (fix/w46-k7-findings): analysed (the frames stopped at the leave, every later line is the keepalive's), diagnostics added (keepalive stall lines, keepalive_stall test), 4 more attempts did not reproduce it: not-reproduced, the known issue stays with the correction.
