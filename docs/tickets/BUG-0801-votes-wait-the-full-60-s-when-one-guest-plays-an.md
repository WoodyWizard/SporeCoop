---
id: BUG-0801
title: Votes wait the full 60 s when one guest plays another world (3-4 players)
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-own-world
commits: dda0afe5
tests: own_creature
signature:
docs: docs/re/host-centric.md#host-centric-own-world, docs/06-testing.md#testing-own-world
related: BUG-0702 BUG-0315
---

## Report
No player report. With 3-4 players and one guest in its own world or the menu, every vote (Cell -> Creature switch, editor votes, Creature -> Tribe) runs into its 60 s timer unless the host presses Start now.

## Root cause
Observed at runtime: with the only guest in a Creature world of its own, the host's vote (`coop.vote test`, the host ready) ran 59 s into its timer (lab 20261112-031435): vote_tick waited for every player, and the guest elsewhere ignores the vote (BUG-0702).

## Fix
vote_tick does not count a player for which own_world::peer_elsewhere(id) holds (host_world::peer_elsewhere: exactly what that guest's own classify() says, unit-tested); a guest in the menu or on a loading screen is still waited for. The change of that count is logged (`vote: N player(s) in another world`). Switch `coop.fix core.vote_present off`.

## Fix idea
Count only players in the vote's place (same_place && PF_IN_WORLD) in vote_tick.

## How to test
Three instances; one guest loads its own world (coop_world_gate_vote.py steps 1-2), host `coop.vote go`. Assert the vote ends within a few s after the present guest's ready; main: `vote end: time is up` after 60 s.

## Verification
Test `own_creature` step 4 (2 instances suffice: the one guest is elsewhere, the host ready).
* FAIL: out/lab/20261112-031435 (`SPORE_FIX_OFF=core.vote_present`, lane 5): the vote ended after 59.0 s.
* PASS: out/lab/20261112-030351, out/lab/20261112-030546: 0.0 s.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-own-world, O1 of 0.7.13): fixed with core.vote_present; FAIL 20261112-031435, PASS 20261112-030351 / 030546.
- 2026-11-12: released in 0.7.13.
