---
id: BUG-0433
title: crash: hosting before the game reaches its main menu faults in the time-of-day sync (TimeOfDay_Unavailable 0x01020F35, GOG 2024)
status: open
kind: crash
stage: All
reported: 2026-11-01
source: players suite creature_fps15burst, lab 20261101-160125 (feat/w33-tests-session)
role: host
versions: main a05d1f6
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at 0x01020F35 (SporeCoop::TimeOfDay_Unavailable+0x5, read 0x10) from game::tod_object <- on_frame time sync, game mode none
docs:
related:
---

## Report
`creature_fps15burst` (players suite, coop_two_instances.py --guest-menu with SPORE_FPS=guest:15 SPORE_NET=burst;
lab 20261101-160125, lane 9): the host instance's world load (load_game.py) failed with a Python traceback,
the test then ran `cheat coop host 47826 Alice` on a game still starting (`game mode: none`), and 3 ms after
`hosting on UDP port 47826` the game crashed: `exception 0xC0000005 at 0x01020F35 Simulator::GetCurrentContext+0x5
(data 0x00000010)` on the main thread (crash-20261101-163115-i17.txt, kept in the lab run folder). 0x01020F30 is
`SporeCoop::TimeOfDay_Unavailable` (overrides_dc04aee5.sym; the crash reporter names the nearest exported
symbol). Symbolized with the build's coop.dll: coop+0xC2D66 = `game::tod_object()` (game.cpp:983), coop+0x50013
= `on_frame` (coop.cpp:2179), the host's once-a-second MSG_WORLD_TIME send. The same harness failure (instance 17
traceback, then the host started early) failed creature_players in lab 20261101-153721 without a crash file.

How to reproduce (inferred from the log, not run by hand): start a game and send `cheat coop host <port> Alice`
on its console before the galaxy menu is up (game mode 0).

## Root cause
Observed (log + symbols): `TimeOfDay_Unavailable` itself reads `[eax+0x10]` with eax 0 while the game has no
simulator yet; `tod_object()` calls it whenever the session is a host, whatever the game mode. Inferred fix: no
time-of-day read (and no MSG_WORLD_TIME) while `game::mode_id()` is 0 or not a stage with a planet. Players host
from the co-op window (galaxy menu or later), so the window of risk is the console or a host started very
early; not seen in a player report.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
