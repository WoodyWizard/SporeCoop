---
id: BUG-0707
title: Space / Creature: the down and regen cooldowns are not reset per session (g_last_down, g_last_regen)
status: wontfix
kind: bug
stage: Space
reported: 2026-11-07
source: K2a audit of BUG-0422 (0.7.11)
role: 
versions: 
fixed_in:
branch: fix/w46-k2a-state
commits:
tests:
signature:
docs:
related: BUG-0422, BUG-0257, BUG-0330
---

## Report
K2a audit of the class BUG-0422 (no player report): teamplay.cpp `g_last_regen` and spacecoop.cpp `g_last_down` are not reset when a session ends.

## Root cause
Read from the code (fix/w46-k2a-state): `g_last_regen` is a 1 s throttle of the pack's regeneration tick (teamplay.cpp tick: `now - g_last_regen < 1.0`); a stale value delays the first tick of a new session by under 1 s, nothing a player sees. `g_last_down` is the 60 s cooldown between two "disabled instead of destroyed" rescues of the own ship in Space; it only counts in the same world (`g_down_epoch_fix`, BUG-0257: `g_last_down_epoch == world_epoch()`), so a new session in another world starts without it. Kept on a quick reconnect into the same world on purpose: otherwise leaving and joining again would skip the cooldown and make the ship undying. Unlike BUG-0330 (the pack's rescue cooldown that outlived worlds), no case where a player loses something.

## Fix
None: by design (see Root cause).

## Verification
no-fail-run: no player-visible effect (a < 1 s throttle; a cooldown already scoped to the world epoch).

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state): closed, by design.
