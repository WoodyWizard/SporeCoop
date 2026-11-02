---
id: BUG-0433
title: crash: hosting before the game reaches its main menu faults in the time-of-day sync (TimeOfDay_Unavailable 0x01020F35, GOG 2024)
status: fixed
kind: crash
stage: All
reported: 2026-11-01
source: players suite creature_fps15burst, lab 20261101-160125 (feat/w33-tests-session)
role: host
versions: main a05d1f6
fixed_in: 0.7.3.4
branch: feat/w33-tests-session
commits: d0cb069
tests: tools/tests/coop_tod_early.py (suite tod_early)
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
`game::tod_object()` (game.cpp), the one door to the time of day for the host's MSG_WORLD_TIME send and the
guest's apply, reads nothing while the game mode is 0 or `Simulator::SpacePlayerData::ptr` is still null.
Switch `core.tod_guard` (BUG-0433), off: the read as before.

Can a player reach it? Checked: the co-op window ("F9 anywhere in the game") first shows in the galaxy menu:
F9 pressed from the boot on and a real click on Host hosted 9.2 s after the start in mode GalaxyGameEntry, no
fault, with the switch off (`coop_tod_early.py --ui`); SpacePlayerData exists there (hosting from the menu is
every test's path). No auto-host or rejoin-at-start exists (coop.ini has none; rejoin.cpp only keeps a token).
So players reach it only through a console line sent during the boot (the dev console accepts connections
from the first moment, its commands run from the first frame: what the creature test did after its load
failed). Not verified: a host quitting the game to the desktop (whether the game frees SpacePlayerData while
frames still run); the guard catches a null pointer there, not a freed one.

## Verification
`coop_tod_early.py` (suite `tod_early`): the lane's host game alone, `coop.fix` asked until coop's commands
exist (the first frame), then `cheat coop host` at once: the line runs on frame 2, game mode 0.
* FAIL: out/lab/20261101-175729 (`SPORE_FIX_OFF=core.tod_guard`): 2 ms after `hosting on UDP port 47826`
  `exception 0xC0000005 at 0x01020F35 Simulator::GetCurrentContext+0x5 (data 0x00000010)`, total faults 1, a
  report file, and the game never reached its galaxy menu (147 s).
* PASS: out/lab/20261101-175959 (and 20261101-175606 / 175631 with the switch on): no fault, the galaxy menu
  in ~15 s, still hosting.

## History
- 2026-11-01: opened.
- 2026-11-02: released in 0.7.3.4.
