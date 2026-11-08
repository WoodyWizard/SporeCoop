---
id: BUG-0708
title: core: a late joiner inherits the host's once-per-save flags
status: not-reproduced
kind: bug
stage: All
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
related: BUG-0422
---

## Report
The audit of BUG-0422 (2026-11-01) inferred that a late joiner inherits the host's once-per-save flags.

## Root cause
Searched again on rel/0.7.11 (fix/w46-k2a-state): the mod's only per-save player flags are the Space finale's `kFlagEnteredCore` / `kFlagCoreTools` on the game's own cPlayer (space_finale.cpp); they are per machine (each game's own player), and `schedule_reward` gives the core gift once to every player that lacks the flag, which is the intended rule. The host's other once-per-save state (social goal payments `g_goal_paid`, keyed by species and outcome; the Space gifts owed, keyed by the world) is shared by the whole party on purpose (a species allied once pays once). No flag of the host is applied to a newcomer as its own.

## Fix
None needed.

## Verification
no-fail-run: no such flag found in the code (space_finale.cpp, social.cpp, spacecoop.cpp gift_owed_tick read).

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state): not reproduced: no concrete flag.
