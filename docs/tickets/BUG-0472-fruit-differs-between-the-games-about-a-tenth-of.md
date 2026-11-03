---
id: BUG-0472
title: fruit differs between the games: about a tenth of the fruit near a player lies elsewhere or exists on one game only (Creature and Tribe)
status: open
kind: desync
stage: Creature
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
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
Detector: `Creature:unmatched:fruit` in 91% of 343 Creature comparisons (3 host / 8 guest fruit without a partner within 3 m of 25), `Tribe:unmatched:fruit` 73%, `guest_only:fruit` up to 27 at once. Fruit falls from trees and is eaten on each game separately (no fruit sync): a fruit one player eats is still there for the other. Class G4 (BUG-0421); tribe food bundles (`Tribe:unmatched:game_bundle` 15%) are the same kind of local drop.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
