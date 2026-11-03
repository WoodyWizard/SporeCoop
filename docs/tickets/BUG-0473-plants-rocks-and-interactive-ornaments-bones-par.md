---
id: BUG-0473
title: plants, rocks and interactive ornaments (bones / part skeletons) near the players differ between the games
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
Detector: `game_plant` unmatched / on one game only in 20-30% of Creature comparisons; `interactive_ornament` (bones, skeletons that give parts) only on the host in 6% (up to 12 in the host's game, 0 in the guest's), `rock` on one game only in 11%. The part pickups match BUG-0459 (a bone picked up by one player is still there for the other): the detector sees the pickups themselves differ. Class G4.
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
