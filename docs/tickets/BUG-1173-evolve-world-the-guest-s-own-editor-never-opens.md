---
id: BUG-1173
title: evolve_world: the guest's own editor never opens after the vote (its game's input stays locked by a cinematic since its own creature was applied at the join)
status: open
kind: test
stage: Creature
reported: 2026-11-15
source: lab 20261115-164240 (feat/w51-core2), baseline 20261115-180220
role: 
versions: 
fixed_in:
branch:
commits:
tests: evolve_world
signature:
docs:
related: BUG-0019, BUG-0404, BUG-0689, BUG-1141
---

## Report
evolve_world FAILS "both players are in their editors" on feat/w51-core2 (lab 20261115-164240, rerun 20261115-174822)
and the same on rel/0.7.15 9362bcdd without that branch (baseline worktree, lab 20261115-180220): the guest's game
says "own editor not opened: the stage did not settle (or was left)" 90 s after the vote. The rest of the test (the
host's migration, the guest's load of the host's world, the guest growing up and migrating) passes.

## Root cause
Observed (guest game log of 20261115-164240): from the join on, after its own creature was applied (`body: own avatar
species ... from this world's record (worlds.ini)`, BUG-0689), the guest's game shows `busy=cinematic,input_lock` in
every trace line until after the host's evolution; the own editor waits for the stage to settle and gives up. The test
confirms the captions it sees only once before the vote. Inferred: a cinematic of the own-creature swap (or a caption
under it) holds the guest's game; not looked into further (not in the C2 / C3 package).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
