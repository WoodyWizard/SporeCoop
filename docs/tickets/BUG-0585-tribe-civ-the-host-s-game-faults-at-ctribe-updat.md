---
id: BUG-0585
title: Tribe -> Civ: the host's game faults at cTribe::UpdateFoodVisuals+0x28 and freezes after the City Hall editor when a guest's food change arrives
status: open
kind: freeze
stage: Tribe
reported: 2026-11-04
source: lab (fix/w43-transitions, coop_tribe_civ.py)
role: host
versions: 0.7.6
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
