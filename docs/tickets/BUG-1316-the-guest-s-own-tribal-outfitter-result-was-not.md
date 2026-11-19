---
id: BUG-1316
title: The guest's own Tribal Outfitter result was not taken: its return hook compared the result's +0x14 (the model type) with the outfitter's editor id, so the game reloaded the shared slot with the guest's outfit
status: open
kind: bug
stage: Tribe
reported: 2026-11-19
source: lab
role: guest
versions: rel/0.7.16
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
- 2026-11-19: opened.
