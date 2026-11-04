---
id: BUG-0521
title: the game does not start with the mod installed: the game's own App::cSporeApp::Init returns 0 and the game exits without a report or a message (Windows build 26300)
status: fixed
kind: bug
stage: Menu
reported: 2026-11-03
source: GitHub issue #193 (2026-11-03)
role: single
versions: 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-gh-issues
commits: 23a436a5, 2331426b, 3dc95658
tests: kit_initfail (tools/tests/kit_initfail.py)
signature: sporekit.log: 'App::cSporeApp::Init -> 0' then 'the game exits (ExitProcess 0) ... frame 1'
docs:
related: BUG-0127
---

## Report
A player (0.7.3.6, GOG 2024, Windows 10.0 build 26300, single player) installed the mod and the game "crashes immediately" every time. Only sporekit.log was attached (no SporeCoop report: none was written) and a screenshot of a Windows notification shown twice (an image link, not seen).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (sporekit.log): SporeKit 0.3.6 and coop 0.7.3.6 boot normally (exe sha256 dc04aee5, symbols, all game fixes on). App::cAppSystem::PreInit and Init return; Init takes 31 s, and SporeKit's watchdog ("no boot progress, stage AppSystem::Init") shows the main thread inside a USER32 message loop (win32u / USER32 frames over the game's resource / property loading), i.e. a modal window was open at that time (inferred: a message box of the game or of Windows). Then the device is made (800x600 window), the first frame runs, and App::cSporeApp::Init returns 0: the game's own start failed and it ended itself with ExitProcess(0) at frame 1. No fault, no error code, so SporeKit wrote no crash report and nothing told the player why.
Observed (Ghidra, GOG 2024 0xF47E90 App::cSporeApp::Init): it returns 0 on many paths (its data file list Spore_DDFList.txt, the editor and UI managers' inits, ...); the log does not say which.
Inferred, not verified: the cause is on the game's side (a damaged or partial install, a data package it cannot read, the Windows insider build); nothing of the mod runs in those steps except its hooks' logging. Not checked: whether the game starts on that PC without the mod.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
SporeKit (gamehooks.cpp, diag.cpp `diag_init_failed`): when App::cSporeApp::Init returns 0, a report and its archive are written (SporeCoop-report-*.zip, named in last-problem.txt: "could not start"), an ERROR log line says the game's own start failed and what to check, and a message box tells the player (start once without mods, repair the game files in GOG Galaxy / Steam, attach the report). Lab instances (instance > 1, or SPOREKIT_NO_DIALOGS) get no box. Switch `kit.fix initfail off` (0.7.3.6: nothing). The cause itself is documented in docs/known-issues.md#issue-game-start-fails.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
kit_initfail.py (suite kit_initfail): a test marker (mods\test-initfail-<instance>) makes SporeKit's Init hook return 0 without calling the game's Init, as on the player's PC (the game then ends with ExitProcess(0) at frame 1, cleanly). A 0 after a full Init is not usable: the game's teardown then faults (tried: out/lab/20261103-194736).
* FAIL with SPORE_FIX_OFF=initfail (kit.fix initfail off from the start, 0.7.3.6): out/lab/20261103-194829 (the game ends, no log line, no report, no mark, nothing at the next start).
* PASS: out/lab/20261103-194901 (log line, SporeCoop-report zip, last-problem "could not start", the next start names it; the coop feed then says "the last game could not start").
Not verified: the message box (lab instances are > 1, so it is not shown there); what fails on the player's PC.

## History
- 2026-11-03: opened from GitHub issue #193.
- 2026-11-03: the report and the message on a failed start (fix/w40-gh-issues, kit.fix initfail); the cause on the player's PC not known.
- 2026-11-03: released in 0.7.4.
