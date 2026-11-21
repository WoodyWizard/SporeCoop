---
id: BUG-9331
title: Lineages Cache: a test game shares its creations unless its typed name carries -NoShare-
status: fixed
kind: bug
stage: all
reported: 2026-11-21
source: owner 2026-11-21
role: all
versions: 0.7.14.1-lineages68-dev
fixed_in: unreleased
branch: fix/l47-test-noshare
commits: a76fff13
tests: tools/qa_win (two TITAN QA games, 2026-11-21, switch off and on)
signature:
docs:
related: BUG-9222, BUG-9329
---

## Report
The owner (2026-11-21): their main install's Lineages Cache holds test creations (Voyager, Joiner and Quad test
creatures, buildings and vehicles, modified Lilku evolutions) that are now being passed on to other players. Test
creatures should never leave the test games: "All tests should all be -NoShare-."

## Root cause
Observed (the owner's cache index, read only, 2026-11-21): 42 received test creations, all received 2026-11-13 to
2026-11-15, shared by QA games whose typed names had no -NoShare- tag (sharers MSIGuest, MSIJoiner, MSI, Titan,
Player). BUG-9222 (2026-11-15) only works by the typed name, and QA sessions kept using untagged names after it (the
tribe QA of 2026-11-21 used MSITribeHost / MSIGuest; between QA games only, so nothing reached a real player then).
Every one of the 42 is already on the 68-dev ban list (BUG-9329: by creation or by its test source), so the owner's
install moves them out at its next start; the owner's own creations (the original Lilku in My Creations) are local
entries the ban never touches. The process gap stays: a test game that forgets the tag shares again.

## Fix
coop.cpp `session_name()`: a game started as a test copy (`-sporekit-profile:` on its command line, which every QA
launcher passes, or SporeKit instance 2+) hosts and joins with "-NoShare-" added to its typed name. library.no_share
(BUG-9222) then does the rest: it offers none of its own creations, and no one keeps a creation it made or passed on.
The tag is never shown (net.shown_names). A real player's game (started by Steam or the launcher) has neither, so
nothing changes for players. Switch `core.test_noshare` (off: the typed name as is, for a deliberate Lineages Cache
sharing test between QA games).

## Verification
Two TITAN QA games (instance 9 profile DuoQAJiffHost hosts, instance 10 DuoQAStageGuest joins, Galaxy menu), names
typed without the tag: `coop.host 47839 TestHost 4`, `coop.join 127.0.0.1:47839 TestGuest`.
- FAIL (`coop.fix core.test_noshare off` on both, 18:47): the guest's log "joined as player 1 (host 'TestHost', 2
  players)", the host's "player 1 'TestGuest' joined": untagged names, so each game shares as a normal player
  (both caches already held each other's creations, so `imported` did not move).
- PASS (on, 18:49): "joined as player 1 (host 'TestHost-NoShare-', 2 players)" and "player 1 'TestGuest-NoShare-'
  joined"; `coop.library` imported 89 / 155 and incoming 0 before and after on both; no_share_unsent counted the
  received creations each game did not pass on. A first build counted that every step (54913 in a minute): fixed
  to once a session (g_offered) and run again.
- no-fail-run: checked by hand on TITAN's QA copy (no lab on Windows); the FAIL / PASS pair is above.

## History
- 2026-11-21: opened.
- 2026-11-21: fixed on fix/l47-test-noshare, FAIL / PASS live on TITAN (Verification).
