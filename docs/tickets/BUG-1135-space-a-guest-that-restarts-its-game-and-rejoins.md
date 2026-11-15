---
id: BUG-1135
title: Space: a guest that restarts its game and rejoins gets none of its kept badges, colonies or budget back (BUG-1107's rejoin-token key is new after a game restart)
status: open
kind: bug
stage: Space
reported: 2026-11-15
source: test
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1107, BUG-1001, BUG-0786, BUG-1132
---

## Report
Found by the C5 lab run (feat/w51-space, 20261115-023819): `space_badges_rejoin` FAILS. The guest leaves, its game is
closed and started again (the test's RESTART=1 default: "a rejoin after a crash or a restart"), it joins again: the host
logs "badges: Bob's table kept: ... (fresh)" instead of sending Bob's kept badges back; the rejoined guest has the
host's badges and rank 0. The same applies to the kept colonies, the cargo hold (when the guest's own disk record,
BUG-0679, does not cover it) and the Rivals budget: everything the host keeps by `spaceext::kept_key`.

## Root cause
BUG-1107 (A10, fix/w51-net) keys the kept state by `net::Session::player_key`, the hash of the HELLO rejoin token. The
token survives a crash (the slot file) and a clean leave within the same process (BUG-1001: memory only), but a clean
leave followed by closing the game empties the slot, so the restarted game sends a fresh token: to the host it is
another game under the same name (exactly what coop_space_kept_key's `keptkey other on` simulates), and it gets nothing.
0.7.14 keyed by the session name, so a restart under the same name got its state back (and two same-name players
swapped it). Not caused by C5: the same FAIL with the hand-built paths of rel/0.7.15 (Verification).

## Fix
Not fixed here (found by C5, outside its package; the key is A10's). It needs a decision on BUG-1001's privacy rule:
the guest could keep the token of a clean leave per host address and name on disk (today: memory only), so the same
game after a restart is the same player to that host; a different game under the same name still is not. Until then
`coop.fix space.kept_token off` gives 0.7.14's name key (restarts work, same-name players swap).

## Verification
- FAIL on rel/0.7.15 d0cdc8ea (the hand-built kept paths, before C5): space_badges_rejoin, run 20261115-025652 of the
  worktree feat/w51-space-base ("Bob's table kept ... (fresh)" after the restart, rank 0).
- FAIL on feat/w51-space with the store: run 20261115-023819 (same symptom).
- PASS on feat/w51-space with SPORE_LAB_FIXOFF=space_badges_rejoin=space.kept_token: run 20261115-025652 of
  feat/w51-space (the store restores by the name key).

## History
- 2026-11-15: opened.
