---
id: BUG-0422
title: class: per-player state under a reused id, and state that existed before a proxy (umbrella)
status: fixed
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2a-state
commits: 6b49ee3d
tests: player_forget (coop_player_forget.py)
signature:
docs: docs/04-netcode.md#netcode-player-life
related: BUG-0706, BUG-0707, BUG-0708, BUG-0487, BUG-0125, BUG-0328
---

## Report
Fixed one module at a time: BUG-0031, 0061, 0064, 0076, 0079, 0109, 0125, 0166, 0234; BUG-0383 dead-first proxies; BUG-0370 newest 24 species.
Siblings not covered: leave fan-out (coop.cpp:3173-3194) misses spacecoop, rivalry, tribe, civ, squadron, space_ext; Space g_last_star / g_partner_down (Jump-to goes to the departed player's star, spacecoop.cpp:1156/1323/:402/:2097) (obs); Rivals g_budget credited to a newcomer (space_rivalry.cpp:67) (inf); guest drops MSG_NPC_STATE for unknown ids and never asks for the spawn (BUG-0328) (obs); late joiner inherits the host's once-per-save flags (inf); g_last_regen (teamplay.cpp:18) and g_last_down (spacecoop) not reset per session (obs); species profile cache keeps the host creature after a leave.
General fix: one per-player registry with join/leave observers keyed by a join serial (or no id reuse in a session); receivers request a resync of unknown ids (rate-limited); per-session reset registry. Outcome check: 3-game id-reuse test; dropped-spawn recovery test.
Source: docs/generalisation-plan.md G5.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
K2a audit (0.7.11) of the sibling list against rel/0.7.11: player ids are reused within a session (net/session.cpp alloc_id: the lowest free id); per-player maps were forgotten by five modules called by hand on a leave.

## Fix
One registry (mods/coop/player_life.h, `core.player_forget`): every module with a map keyed by player id registers its forget / held functions next to the map; coop.cpp calls forget_all on every leave and join. Children: BUG-0706 (fixed: spacecoop, space_rivalry, space_map, space_finale, tribe, civ, fields, creations, realign, effects); BUG-0707 (wontfix: the regen throttle and the Space down cooldown are by design, the latter scoped to the world epoch); BUG-0708 (not reproduced: no once-per-save flag of the host applies to a newcomer); item 4 (unknown NPC ids) fixed earlier by BUG-0487 (npc.resync); item 7 (species profile cache) not applicable: the game's profiles are process-wide, the mod's maps are cleared (creations g_design_sent now in the registry). BUG-0328 (a rejoining Tribe guest's NPC coverage) is in N2.

## Verification
Through BUG-0706: coop_player_forget.py FAIL with `SPORE_FIX_OFF=core.player_forget` out/lab/20261107-223012, PASS out/lab/20261107-222542, out/lab/20261107-223602. Detector: no signature of this class (tools/detector_report.py KNOWN has none; 400 runs of 2026-11-06/07 across the worktrees, 6324 comparisons).

## History
- 2026-11-01: opened.
- 2026-11-07 (fix/w46-k2a-state, K2a of 0.7.11): the shared registry; children BUG-0706 fixed, BUG-0707 wontfix, BUG-0708 not reproduced; umbrella closed.
- 2026-11-08: released in 0.7.11.
