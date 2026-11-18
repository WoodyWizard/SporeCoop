---
id: BUG-1262
title: Adventure: the partner's puppet has this game's own captain's look, not the partner's captain (the captain's species is made per game)
status: open
kind: bug
stage: Adventure
reported: 2026-11-18
source: fix/w53-advorn: found while fixing BUG-1254
role: both
versions: <=0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1254, BUG-0793
---

## Report
Lab 20261118-130637 (adventure_tutorial, the BUG-1254 fix on): each game shows its partner in the tutorial adventure as a
second copy of its own captain (key frames 02_played: two pink captains on the guest, two green ones on the host), at the
right size.

## Root cause
* Observed (runtime): the captains' species keys are made per game (host 2927BAE4, guest 2927BAE2 in one run; 2927B941 /
  2927B940 in another; the same key 2927BD73 for two different captains in lab 20261118-131729): the partner's key does
  not name its captain in this game, so the BUG-1254 fix (adventure.puppet_captain) uses this game's own captain as the
  stand-in.
* Inferred: showing the partner's captain needs its creation (the captain's creature records) sent like the Creature
  stage's creations (creations.cpp bundles) and a species built from it here; part of co-op adventures (BUG-0793).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened (fix/w53-advorn, found while fixing BUG-1254).
