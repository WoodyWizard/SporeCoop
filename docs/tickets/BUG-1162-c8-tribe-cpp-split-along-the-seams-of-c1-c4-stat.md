---
id: BUG-1162
title: C8: tribe.cpp split along the seams of C1-C4, state grouped into structs, debug() in its own file
status: open
kind: tooling
stage: Tribe
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch: feat/w51-tribe
commits:
tests:
signature:
docs: docs/01-architecture.md#arch-tribe-files
related: BUG-1128, BUG-1140, BUG-1143, BUG-1160
---

## Report
Plan item C8 of docs/plan-0.7.15.md for Tribe (audit STG-7): tribe.cpp, 7.9k lines with 300+ file-scope globals, its
debug() of 1.4k lines in the shipped file; every wave touched it.

## Root cause
Structure: one hub file.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
