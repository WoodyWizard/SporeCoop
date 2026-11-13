---
id: BUG-1044
title: fields: PLAYER_STATE("fields") erases and counts g_stats by player id, but g_stats is keyed by kind id (diagnostics)
status: open
kind: test
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-8
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0422
---

## Report
Audit SPC-8 (second half). Not seen at runtime, found by code reading. Diagnostics only: when a guest with player id 1..3 leaves, the host's fields statistics of kind 1..3 (K_NPC, K_TRIBE_MEMBER, K_UFO) are erased, and the departed-player state check counts a kind's stats as state left behind for the player with the same number.

## Root cause
Observed in code: mods/coop/fields.cpp:34 `g_stats` is a map kind id -> Stats (filled at 158, 185, 260); fields.cpp:43-44 `PLAYER_STATE("fields", ...)` calls `g_stats.erase(id)` and adds `g_stats.count(id)` with the player id. Kind ids are 1..11 (fields.h:58-68). No game state is touched. Confidence: high.

## Fix idea
Drop g_stats from PLAYER_STATE (it is not per player); only g_sent is per player. No switch needed (diagnostics).

## How to test
A state-leak check after a guest leaves (the PLAYER_STATE sum used by the BUG-0422 test, `coop.state` or the existing player-leave test): with a session that relayed NPC fields, the departed id 1 must report 0 entries for `fields`; main reports 1 (FAIL).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
