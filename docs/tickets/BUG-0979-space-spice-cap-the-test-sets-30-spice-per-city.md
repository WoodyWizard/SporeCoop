---
id: BUG-0979
title: space_spice_cap: the test sets 30 spice per city, over the game's cap of 5; the game clamps it back and the guest shows 5 (1 of 3)
status: fixed
kind: test
stage: space
reported: 2026-11-12
source: lab-final-0.7.13
role: guest
versions: 0.7.13
fixed_in: unreleased
branch: fix/w48-fmisc
commits: ea7552b3
tests: space_spice_cap
signature: space_spice_cap: FAIL the guest's game has the host's spice in colony 00460014 (the 3th of the list): [5.0]
docs: docs/re/space.md#space-spice-runtime-clamp
related: BUG-0834, BUG-0537
---

## Report
The 0.7.13 final check (lab 20261112-175447) and its rerun (20261112-205503, #1 of 2) failed space_spice_cap: "the
guest's game has the host's spice in colony 00460014 (the 3th of the list): [5.0]". Not a player-visible desync:
both games ended with 5.0.

## Root cause
Observed: the test set 30 spice per city on the host's colony 00460014 (one city), over the game's cap (5 per city,
docs/re/space.md#space-spice-runtime-clamp). The game clamps it back within seconds while running: in 175447 the
host's game did (the guest wrote 30, then the host's 5: `written=2`); in 205503 #1 the guest's game did and
colonyspice read the drop as a collection of 25 (`this game collected spice of colony 00460014: the host is told`,
host `player 1 collected 25 spice ... taken here too`). The one PASS read 30 before the clamp. The sync itself was
right in all three runs.

## Fix
Test only (tools/tests/coop_space_colony_spice.py, case cap): the host sets 1.5 per city (under the cap), and the
check is that the guest's cities equal the host's (within 0.3, production may add a little) and are under 3.5 (not
the old 5). No code change.

## Verification
FAIL (SPORE_FIX_OFF=space.colony_spice_all, the test still catches BUG-0834): out/lab/20261112-212337 (guest [5.0],
host [1.5]). PASS: out/lab/20261112-212500 (space_spice_cap 3 of 3, lane 6).

## History
- 2026-11-12: opened; test value under the game's cap (fix/w48-fmisc).
