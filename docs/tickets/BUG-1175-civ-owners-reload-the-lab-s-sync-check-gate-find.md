---
id: BUG-1175
title: civ_owners_reload: the lab's sync-check gate finds vehicles of the cities founded during the test missing on the guest (rel/0.7.15 too)
status: open
kind: test
stage: Civ
reported: 2026-11-15
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
tests: civ_owners_reload
signature:
docs:
related: BUG-0319, BUG-0380, BUG-0950, BUG-0647
---

## Report
civ_owners_reload's own checks pass (RESULT: OK), but the lab's sync-check gate fails it: "MISMATCH with the host ...:
v: 1-2 missing on the guest" about 45 s into the session, right after the host founded two cities ("the host has 6
cities, this game 4 and 6 founded (on their way)"). The missing vehicles are those of the new cities: the guest asks the
host for their design ("creations: asked the host for the vehicle design F0F41CEC") only when its own copies of the
cities appear, a moment before or after the check.
Seen on feat/w51-civ (lab 20261115-174639, 20261115-190127, 20261115-192303 x2) and on rel/0.7.15 9362bcdd without
any change of that branch (out/lab/20261115-192303-base9362 x2, copied from a worktree of 9362bcdd): not a change of
feat/w51-civ. An earlier pass: feat/w51-store 20261115-140056 (the check ran before the host's new vehicles existed).

## Root cause
Inferred: a timing race between the sync-check (about 45 s after the start) and the guest's catch-up of the cities the
host founded during the test (BUG-0319 / BUG-0380 class, the founding waits while a capture cinematic holds the game)
plus the design request for the new nation's vehicles (BUG-0647). Not analysed further in this branch.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
