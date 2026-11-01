---
id: BUG-0393
title: Lab gate: sync-check MISMATCH 'p: 1 apart' in Tribe while a player's view jumps (the RTS player point, 20-70 m)
status: open
kind: test
stage: Tribe
reported: 2026-11-01
source: lab gates (feat/w30-lab-gates) on fix/w30-tribe-walk and fix/w31-* branches
role: both
versions: unreleased
fixed_in:
branch:
commits:
tests: chaos_tribe, tribe_walk_mobile
signature:
docs:
related:
---

## Report
With the log gates on, `chaos_tribe` (out/lab/20261101-104002, lane 3) and `tribe_walk_mobile`
(out/lab/20261101-101600 #1, lane 3) failed on `gate sync-check`: "sync-check: MISMATCH with the host
(Tribe A3930F03, 38 objects compared): p: 1 apart (p0 69.9)" on both games, the 3 s recheck too. Every
tribe member (t) and animal (n) matched. The same line is in other branches' Tribe runs today
(fix/w31-* lab dirs, p0 27.7 / p1 19.7 / p0 40.8, also 02:08 before these branches).

## Root cause
Inferred: in Tribe a player's `p` entry is its RTS point (the view / cursor marker, rts_drive), which jumps
tens of metres when the player moves the view by the minimap (both tests do, every second or so); the
other game's copy is a state behind (over a mobile network 120-180 ms more). Not checked in diag_sync.cpp
which point `p` is in Tribe.

## Fix
Open: leave `p` out of the Tribe / Civ comparison (or compare it with a tolerance for a view jump), or
allow the gate in suite.py with this ticket.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
