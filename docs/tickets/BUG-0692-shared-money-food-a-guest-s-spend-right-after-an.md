---
id: BUG-0692
title: Shared money / food: a guest's spend right after an editor visit is undone by the next host total (the sync state dropped on the mode switch)
status: open
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0423 (0.7.11)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0423
---

## Report
From the K2a audit of BUG-0423. shared.cpp tick sets have=false whenever the game mode is not the resource's (an editor visit: kEdit / kEditor). Back in the stage, the guest waits for the host's next total with have=false, and a spend made in that window (up to 2 s) is not folded: the host's total overwrites it on the guest's screen (the purchase made, the money back: spent twice) and the host never counts it.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
