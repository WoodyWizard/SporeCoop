---
id: BUG-0273
title: vote: the I'm ready clicks of all three guests were not counted, the Creature -> Tribe vote ended by its timer
status: open
kind: bug
stage: creature
reported: 2026-10-29
source: lab 20261029-122937 campaign
role: 
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Lab 20261029-122937 campaign#1 (lane 2, out/lane2/campaign/20261029-122947, finding 3). The host
started the Creature -> Tribe vote at 13:49:15 (game clock). Bob, Carol and Dave clicked the real
"I'm ready" widget at 3-7 s (summary 12:49:18-22, Carol and Dave twice), but the host logged nothing
until "evolution starts (time is up)" at 13:50:14; Bob's log has no line of the click either. The
first vote of the same game (Cell -> Creature, 13:39) ended "everybody is ready" in 2 s. The vote
came right after the EP shortcut and three captions per player. Not analysed (?): whether the
widget click reached the mod (button shown but the vote not yet known on the guest?).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
