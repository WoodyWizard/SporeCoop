---
id: BUG-1236
title: Civ: a guest gets an 'X earned N money (shared)' card every 6 s through the whole stage (the cities' income ticks of 40-380 pass kCauseMin 25)
status: open
kind: bug
stage: Civ
reported: 2026-11-18
source: key-frame review of final check 20261117-154911
role: 
versions: 0.7.10, 0.7.14.1, unreleased
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-civ-money-cards
related: BUG-0696
---

## Report
Key-frame review of the second 0.7.15 final check (out/frames-0.7.15-b.md, finding 2): guests get the card every 6 s through the Civ stage, 698 lines in the guests' logs of lab 20261117-154911 (632 in 20261116-025154). mods/coop/shared.cpp (BUG-0696) says the cities' income stays under kCauseMin = 25; the income ticks are 40-380.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened from the key-frame review; old class, left for the next release.
