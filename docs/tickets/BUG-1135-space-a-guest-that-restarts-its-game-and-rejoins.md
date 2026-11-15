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
related:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
