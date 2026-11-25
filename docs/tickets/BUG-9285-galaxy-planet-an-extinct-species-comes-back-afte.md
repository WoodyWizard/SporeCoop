---
id: BUG-9285
title: galaxy planet: an extinct species comes back after the server's refresh time (the nest refresh)
status: fixed
kind: feature
stage: creature
reported: 2026-11-20
source: step 4 part 4 (docs/12 #server-nest-refresh, 6Z)
role: host
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch:
commits: 3ea9d832
tests: tools/tests/galaxy_server_check.py (server side); in game: TITAN QA runs (docs, Verification)
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
TITAN QA runs (no lab on Windows), docs/12-server-and-players.md#server-nest-refresh-built:
- FAIL (switch `creature.nest_refresh` off, clock +3 h): all 5 extinct species stayed extinct.
- PASS (on): the host revived all 5; the guest the 2 within its nearest nests at once; 7 animals and the game's own
  goal at a revived nest (images/stage4_nest_refresh_revived.png).
- tools/tests/galaxy_server_check.py: WELCOME refresh field, PONG clock.

## History
- 2026-11-20: opened.
- 2026-11-20: built and seen in game.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
