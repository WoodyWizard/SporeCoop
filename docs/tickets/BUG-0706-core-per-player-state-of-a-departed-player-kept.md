---
id: BUG-0706
title: core: per-player state of a departed player kept under its id by spacecoop, space_rivalry, space_map, space_finale, tribe, civ, fields, creations, realign, effects (ids are reused)
status: fixed
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0422 (0.7.11)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2a-state
commits: 6b49ee3d
tests: player_forget (coop_player_forget.py)
signature:
docs: docs/04-netcode.md#netcode-player-life
related: BUG-0422, BUG-0125, BUG-0076, BUG-0064
---

## Report
K2a audit of the class BUG-0422 (items 1-3, 7): the host gives a newcomer the lowest free player id (net/session.cpp alloc_id), so the next player to join after a leave gets the departed player's id. The leave fan-out called five modules by hand; spacecoop (the last star: Jump-to went to the departed player's star; its down timer kept the newcomer out of rescues), space_rivalry (its budget credited to a newcomer in a merge), space_map (roster, colonies, the newcomer not sent everybody's rosters), space_finale, tribe (chief, pincer hits), civ (siege hits), fields (values last sent: a newcomer got only changed ones), creations (designs answered), realign and effects kept their state under the id.

## Root cause
Observed (lab 20261107-223012, `SPORE_FIX_OFF=core.player_forget`): after the guest's leave the host still held `space_finale 1, spacecoop 1` of player 1 (space_map prunes its own rosters). The other modules by reading the code (maps erased only at session end).

## Fix
`mods/coop/player_life.h`: every module with a map keyed by player id registers it (`PLAYER_STATE(name, forget, held)` next to the map); coop.cpp calls `player_life::forget_all` on every leave and every join (`core.player_forget`), so a module added later cannot miss the fan-out. `coop.players held <id>` lists what is held.

## Verification
coop_player_forget.py (fixture space: the guest leaves and joins again):
* FAIL with `SPORE_FIX_OFF=core.player_forget`: out/lab/20261107-223012.
* PASS: out/lab/20261107-222542, out/lab/20261107-223602.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state): fixed, core.player_forget.
- 2026-11-08: released in 0.7.11.
