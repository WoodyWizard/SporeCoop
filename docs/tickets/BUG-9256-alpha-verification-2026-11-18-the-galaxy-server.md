---
id: BUG-9256
title: Alpha verification 2026-11-18: the galaxy server, parties and scaling review fixes
status: fixed
kind: bug
stage: Menu
reported: 2026-11-18
source: verification
role: 
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 88e1dc00
tests: tools/tests/galaxy_server_check.py, tests/party_tests.cpp, tests/validate_tests.cpp, tests/net_tests.cpp
signature:
docs: docs/13-alpha-review-2026-11-18.md
related:
---

## Report
The owner asked for a verification of the whole alpha branch (2026-11-18). Three independent code reviews, the unit
tests, the lints and a live run found the list in docs/13-alpha-review-2026-11-18.md: this ticket stands for the
ones fixed in that pass (S1, S3-S5, S7-S9, S11-S14, P1-P3, P5-P7, P9-P12, W1-W3, W5-W8, W10 and the broken tests).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Each finding has its cause in the review file (one line each).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
See the review file's status column. Not fixed there: decision 6S (identity, address proof, planet ownership) and the
open items listed under "What is left".
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
tools/tests/galaxy_server_check.py: FAIL on the server built from the commit before (9 of 15 checks, GALAXY_CHECK_OLD=1),
PASS on the fixed one (15 of 15), both 2026-11-18 on TITAN. no-fail-run for the game-side fixes: found by code reading;
the unit tests (21 suites) and a three-game galaxy run (host from the stored world, stored-copy join, party, hand-over)
pass with them; the review file lists what was not run.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
