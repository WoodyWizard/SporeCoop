---
id: BUG-0757
title: Tribe: an NPC proxy walked at 6.5 m/s is drawn one guest frame behind its target at 15 fps (tribe_npc_dash: driving p50 0.42 m, p90 0.68-0.77 m; 90 % within 1.36 m, limit 1.2 m)
status: open
kind: desync
stage: Tribe
reported: 2026-11-08
source: fix/w46-k4-proxy lab 20261108-102113
role: guest
versions: 
fixed_in:
branch:
commits:
tests: tribe_npc_dash
signature:
docs:
related: BUG-0753, BUG-0281, BUG-0365
---

## Report
tribe_npc_dash on fix/w46-k4-proxy (BUG-0753's fix: the states are now the host game's positions with the dash's
velocity), lab 20261108-102113 (lane 7, guest 15.3 fps, host 64-67 fps): 1 of 3 PASS; the two failures 90 % within 1.36 m
(limit 1.2 m). The split: the guest's target from the host's NPC p90 0.57-0.63 m (the states and the lead are right; before
BUG-0753's fix 1.0-3.2 m), the drawn proxy from its target p50 0.42 m, p90 0.68-0.77 m: a constant one guest frame (6.5 m/s
x 65 ms) behind its target in the failing runs, p50 0.09 m in the passing one (other NPCs of the herd, n2 / n10). The same
split was in G4's runs before (half target, half driving ~0.67 m, fix/w46-g4-tests lab 20261108-082630). Not looked into:
walk_place for Tribe proxies (own_height, ground, catch_prop) and when the drawn position is read against the target.
How to reproduce: tools/lab.py run tribe_npc_dash --repeat 3 --retry 0 (lanes 6 / 7).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
