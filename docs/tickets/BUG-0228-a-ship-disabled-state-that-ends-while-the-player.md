---
id: BUG-0228
title: a "ship disabled" state that ends while the player is in an editor or an adventure leaves the ship invincible
status: fixed
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #162 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-space
commits: 7c9ee00, 6dbb476
tests: space_down_away (coop_space_down_away.py part c)
signature:
docs: docs/re/space.md#space-object-serial
related: BUG-0229, BUG-0098, BUG-0147, BUG-0217
switch: space.down_clear
---

## Report
From GitHub issue #162: https://github.com/WoodyWizard/SporeCoop/issues/162 (the full analysis, links and the scenario are there).

Since the BUG-0098 fix (30a25b0, release 0.7.3.0), ending the co-op "ship disabled" state clears the ship's invincible byte only while the game is in the Space mode. A session that ends while the player is in an editor or an adventure entered from Space leaves the byte at 1, because the Space game and the ship stay alive there, and the ship then stays invincible. Severity LOW, a small regression.

## Root cause
Observed: `clear_down_invincible()` wrote the byte only when `in_space()` held at the moment the state ended (session end, `disable 0`). With the guest acting as outside Space (`coop.space away 1`, what an editor or an adventure gives the mod) and the session ending, the ship stayed invincible afterwards: `hit 20` did nothing (lab run 20261028-225138 with the fix off).

## Fix
The disabled state records its ship (pointer, `game::object_serial`, `spaceext::world_epoch`). Clearing the byte is "now or owed": at once when that very ship is the live player ship of the same Space world and no cinematic plays, else `g_invincible_owed`, checked at the top of `spacecoop::tick` before the `!in_space() || !connected()` return (also after the session ended); another world or a gone ship drops the owed clear without a write. Switch `space.down_clear` (off = 0.7.3.0). `spaceext::world_epoch` is a Space-only stand-in for #151's stage epoch (BUG-0217, not on this branch).

## Verification
coop_space_down_away.py part (c): PASS out/lab/20261028-224335 (and the later PASS run listed in BUG-0229); FAIL with `SPORE_FIX_OFF=space.down_world,space.down_clear` out/lab/20261028-225138 (`(c) the ship is no longer invincible` failed; that run also had (a) kill the ship first, see BUG-0229). Not done: a real editor trip during a disable (the `away` hook stands in for it).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-space (7c9ee00): the clear waits for the first Space frame on the same ship; test space_down_away.
- 2026-10-29: released in 0.7.3.1.
