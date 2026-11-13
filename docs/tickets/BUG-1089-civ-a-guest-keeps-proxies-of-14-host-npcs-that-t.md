---
id: BUG-1089
title: Civ: a guest keeps proxies of 14 host NPCs that the host's game has at the planet's centre (0 0 0), 525 m apart (sync-check MISMATCH)
status: open
kind: bug
stage: Civ
reported: 2026-11-13
source: lab
role: guest
versions: 
fixed_in:
branch:
commits:
tests: campaign (lab gate sync-check)
signature:
docs:
related: BUG-0339, BUG-0274, BUG-0973
---

## Report
fix/w49-camp, the full campaign lab 20261113-160426 (lane 4): the campaign itself went Cell -> Space and the finale
the players' way with no jump (RESULT: OK); the lab failed it on the sync-check gate in Civ, 26 s after Carol's rejoin
(the host's share save at 17:00:00): "sync-check: MISMATCH with player 1 'Bob' (Civilization 9652F8AD, 2 checks 3 s
apart, 60 objects compared): n: 14 apart (n1274 526.6 n1275 524.8 ...)". The host's game has those NPCs (species
067A147A, hp 8-16, `upd=64/64 moved64=0.0`) at `pos=(0.0 0.0 0.0)`, the planet's centre; Bob's proxies of them were
made 7.8-8.1 s before (`spawned ... age=7.86`) at their last wanted place on the ground. BUG-0339's picture (fixed for
Creature) in Civ.
Reproduce: the full campaign (`lab.py run =campaign`), look for "n: .. apart" with "pos=(0.0 0.0 0.0)" on the host.

## Root cause
Not analysed (outside fix/w49-camp's package). Inferred: Civ NPCs of a herd the host's game keeps (BUG-0973's host-owned
wild creatures) parked at the centre while they are not in play, still listed for the guests.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
