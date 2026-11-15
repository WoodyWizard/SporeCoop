---
id: BUG-1128
title: B2: coop.cpp split into coop_cmds, dna, vote, puppets, session_glue by pure moves; sporekit_mod_main and the frame in named phases
status: open
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch: feat/w51-split
commits:
tests:
signature:
docs:
related: CORE-2 CORE-8 (docs/audit-0.7.15/core.md)
---

## Report
Plan 0.7.15 B2 (docs/plan-0.7.15.md, audit CORE-2 / CORE-8): mods/coop/coop.cpp was 8.6k lines with about 300 globals in
one anonymous namespace, touched by 42% of the bug tickets and the main merge-conflict point of parallel branches;
sporekit_mod_main (1.2k lines) and on_frame (200 lines, about 80 calls in an order bug fixes depend on) kept their
order only in comments.

## Root cause
Structure, not a bug: every concern of the core (commands, DNA routing, vote and own editor, puppets, session glue)
shared one file and its globals.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
