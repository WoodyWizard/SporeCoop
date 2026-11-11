---
id: BUG-0810
title: Space: a guest's kept colonies are not restored after a crash rejoin with the same player id (the kept list is overwritten by its empty roster)
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0195
---

## Report
A guest whose game crashed and who rejoined at once finds the colonies it had founded in the co-op galaxy gone, in the case BUG-0195 was made for. Found by reading the code.

## Root cause
Observed in the code, confidence high for the path (that alloc_id gives back the same id is inferred). space_map.cpp:548 kept_roster sends K_KEPT only when `k.id != id`; k.id goes back to 0xFF only in kept_tick (:579-581) when the id is missing from players() in some frame. On a crash rejoin session.cpp:465-472 drops the old peer and accepts the new one in the same handle_msg call, and alloc_id gives the lowest free id (the same one): no frame sees the player missing, no K_KEPT goes out, and kept_roster stores the guest's new (empty) roster as its kept planets. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Mark the kept entry as left (id = 0xFF) on the session's player-leave event (PLAYER_STATE / on_player_leave), or key a new join by a join serial instead of the player id.

## How to test
coop_space_kept-style test: guest `coop.space map colonize`, host `coop.space map kept` lists it; kill the guest's lane (`tools/spore.sh kill`), restart and rejoin within the token window. FAIL = `coop.space map kept` on the host shows `sent` not rising and the list empty, the guest has no colony; PASS = K_KEPT sent and the colony back.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
