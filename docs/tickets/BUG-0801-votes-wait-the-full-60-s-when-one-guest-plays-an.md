---
id: BUG-0801
title: Votes wait the full 60 s when one guest plays another world (3-4 players)
status: open
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0702 BUG-0315
---

## Report
No player report. With 3-4 players and one guest in its own world or the menu, every vote (Cell -> Creature switch, editor votes, Creature -> Tribe) runs into its 60 s timer unless the host presses Start now.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): vote_tick (coop.cpp:2275-2277) waits for every player in g_sess.players(); since BUG-0702 a guest elsewhere ignores the vote (coop.cpp:4317-4322) and never sends MSG_READY; the BUG-0315 auto-ready of arrived guests (2263-2274) does not cover it. Confidence: high; impact medium-low (Start now exists).

## Fix
Not fixed yet.

## Fix idea
Count only players in the vote's place (same_place && PF_IN_WORLD) in vote_tick.

## How to test
Three instances; one guest loads its own world (coop_world_gate_vote.py steps 1-2), host `coop.vote go`. Assert the vote ends within a few s after the present guest's ready; main: `vote end: time is up` after 60 s.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
