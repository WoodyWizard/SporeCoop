---
id: BUG-0550
title: Creature: an NPC 9 m over the ground on the host is drawn on the ground on the guest (sync-check n apart 9.3, want_agl 10.56 made_agl 0)
status: open
kind: desync
stage: Creature
reported: 2026-11-04
source: final check out/lab/20261104-135838 play_creature
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0532
---

## Report
play_creature in the 0.7.6 final check (out/lab/20261104-135838): `sync-check: MISMATCH ... n: 1 apart (n34 9.3)` on both games while the host's creature was dead (cinematic). Host: n34 species 065CB956, agl 9.31, moving 6.9 m/s. Guest: the NPC puppet `want_agl=10.56 made_agl=0.00 grounded=0`, drawn on the ground. Seen once (1 of 2 play_creature runs on rel/0.7.6). Reproduce: `tools/lab.py run play_creature --repeat 3`, look for `n: 1 apart` with an agl over 5 m on the host.

## Root cause
Not analysed yet. Inferred: an NPC in a jump or a flight (agl 9 m) on the host; the guest's NPC puppet keeps the ground height. Compare BUG-0532 (the player's puppet in the air).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
