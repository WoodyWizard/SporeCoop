---
id: BUG-0810
title: Space: a guest's kept colonies are not restored after a crash rejoin with the same player id (the kept list is overwritten by its empty roster)
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-space
commits: 173e2078
tests: space_colony_crash
signature:
docs: docs/re/space.md#space-s1-0713
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
The host's kept entry of a player is marked as left (id 0xFF, not restoring) by the session's leave through
player_life (PLAYER_STATE "space_map_kept", called on PLAYER_LEAVE and on every join of that id). A crash rejoin drops
the old entry (drop_peer: PLAYER_LEAVE before PLAYER_JOIN, session.cpp) in the same message that accepts the new HELLO,
so the per-frame check of kept_tick never saw the player missing; now the leave event itself does it. Root cause
confirmed at runtime: with the switch off the rejoined guest (same id 1, rejoined by its token 9-10 s after the
SIGKILL) got nothing back (`kept: sent=0`, its colony tech 1) and the host's list became its empty roster
(`colonies=0`). Switch `coop.fix space.kept_leave off` (0.7.12). `coop.space map kept` shows `left=`.

## Verification
`coop_space_colony_rejoin.py --crash` (suite `space_colony_crash`): the guest colonizes a planet of the home system,
its game is killed (SIGKILL: the rejoin token stays) and started again at once, it joins within the 30 s timeout.
* FAIL with `SPORE_FIX_OFF=space.kept_leave`: lab 20261112-032648 (the rejoined guest's planet 01460035 tech 1, no
  civ; host `kept ... sent=0`, then `colonies=0`).
* PASS: lab 20261112-032653 (`left=1 sent=1 done=1`, the colony tech 5 with the guest's pol again).
* PASS again: lab 20261112-040253; the area's older tests pass on the branch (lab 20261112-041200, 20261112-041012).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-space (the leave event marks the kept entry; switch space.kept_leave).
- 2026-11-12: released in 0.7.13.
