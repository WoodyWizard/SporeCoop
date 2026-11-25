---
id: BUG-9379
title: The galaxy server's update follows the game on its PC
status: fixed
kind: feature
stage: Galaxy
reported: 2026-11-24
source: decision 8Z (owner 2026-11-24)
role: host
versions: 0.7.14.1-lineages71-dev
fixed_in: 0.7.14.1-lineages72-dev
branch: feat/lineages-alpha
commits: 155a04d4
tests: tools/qa_win (manual): Start-Galaxy-Server.cmd and galaxy_update.exe against a stand-in release on 127.0.0.1 (docs/06-testing.md#testing-server-update-check; scratchpad make_web.py, run_start.ps1, run_timer.ps1)
signature:
docs: docs/12-server-and-players.md#server-update-follows-game
related: BUG-9378
---

## Report
71-dev's update check (BUG-9378, 8Z A as I had applied it) asked the host and updated by itself after 30 seconds.
The owner's answer to 8Z, 2026-11-24: "Let's check if the local install is updated, if it is, automatically update.
If it isn't, then ask and after the timer, default to no so the player can play without interruption."

## Root cause
Not a bug: a decision. The reason behind it: the server and every game must be the same build. A server that
updates itself while the host's own game (and the friends') is still on the old build locks all of them out until
they update, in the middle of an evening; a server that stays old while the host's game is already new cannot be
joined by its own host.

## Fix
* galaxy_update.exe `--check` also reads the version of the game in its game folder
  (SporebinEP1\mods\sporecoop.version, upd::installed_version) and answers 11 when a newer release is out and that
  game already has it, 10 when the game is still older (or there is no game folder), 0 as before.
* Start-Galaxy-Server.cmd: 11 updates without a question; 10 asks "Update the galaxy server now (N by itself in 10
  seconds)": no answer is no and the server starts as it is. GALAXY_UPDATE: ask (this rule, the default), auto
  (always update, no question), off (no check).
* The timer is 10 seconds, not 30: with no as the answer the question is only a wait before the server starts, at
  every start until the host updates.
* A 71-dev server keeps 71-dev's behaviour (ask, yes after 30 seconds) until it has updated itself once.

## Verification
TITAN, 2026-11-24 about 20:05, a dummy game folder with the server, a stand-in release "0.7.14.1-lineages72-dev" on
127.0.0.1:8971 (SPORECOOP_UPDATE_API); the game's and the server's version files set for each case:
- `--check`, game 71, server 71: "Lineages 0.7.14.1-lineages72-dev is out (this server: ..71.., the game on this
  PC: ..71..)", exit 10. Game 72, server 71: "... and the game on this PC already has it", exit 11.
- The start file, game 71, server 71, mode ask: no input: "[Y,N]?N", the server runs, its files still 71. In a
  console of its own with nobody typing: the same after the timer (the server started 10.7 s after the start
  file). Answer Y: the update, the server runs as 72.
- Game 72, server 71, mode ask, no input: no question, the update, the server runs as 72.
- Mode auto, game 71: the update without a question. Mode off: no check line, the server runs.
- A pipe that stays open and silent is no test of the timer: `choice` does not count down on a pipe (seen).
no-fail-run: a feature (the behaviour before is 71-dev's, described above).

## History
- 2026-11-24: opened.
- 2026-11-24: built on feat/lineages-alpha (8Z, the owner's rule), not released yet.
- 2026-11-24: released in 0.7.14.1-lineages72-dev.
