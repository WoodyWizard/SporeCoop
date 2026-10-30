---
id: BUG-0290
title: tests: the campaign bot answers the same Civ talk dialog for 20 min and civ -> space times out
status: open
kind: test
stage: Civ
reported: 2026-10-29
source: final campaign lab 20261029-203821 campaign_space#1 lane 7
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
Lab 20261029-203821 campaign_space#1 (lane 7, run out/lane7/campaign/20261029-215906): in civ/advance the bot logged "Alice: talk answered" / "Carol: talk answered" every 12 s for 20 min, the host never reached Space and the test jumped with the fixture. A dialog the CIV_TALK list in tools/tests/coop_campaign.py does not close (see BUG-0265 for the earlier case). A test problem, not a mod bug as far as seen.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
