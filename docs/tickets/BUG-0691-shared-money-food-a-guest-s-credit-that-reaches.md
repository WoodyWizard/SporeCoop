---
id: BUG-0691
title: Shared money / food: a guest's credit that reaches the host while the host cannot read the resource (load, menu) is never counted or acked
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
From the K2a audit of BUG-0423 (no player report). shared.cpp on_message returns before the ack when the host's game cannot read the resource (`if (!r.get(cur)) return;`): the host in a load, the galaxy menu or without its player tribe / civilization. A guest's earned Civ money or Tribe food sent then (MSG_RES_ADD) is never counted, never acked: the guest keeps it in flight and shows it, the host never has it; both games' totals differ from then on.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
