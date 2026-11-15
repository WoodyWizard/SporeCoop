---
id: BUG-1132
title: C5: Space kept state (colonies, cargo hold, badges, Rivals budget) through one KeptStore with one handshake and restore window
status: open
kind: tooling
stage: Space
reported: 2026-11-15
source: plan
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
Plan 0.7.15 C5 (audit NET-1): a rejoining Space guest's own state is kept by the host and handed back by four
hand-built protocols (space_map K_KEPT/K_KEPT_DONE, space_trade K_INV/K_INV_KEPT/K_INV_DONE, space_badges
K_BADGES/K_BADGES_KEPT/K_BADGES_DONE, space_rivalry K_ASK/K_SET), each with its own table, serial, timeout, leave rule
and restore window; bug class L (state outliving its context), 37 tickets, most of 0.7.13-0.7.14.

## Root cause
The same state machine written four times (BUG-1041 was one of them missing in a list three times).

## Fix
`mods/coop/space_kept.h` (portable, unit tests first): `KeptStore<T>` (host: per player key, world clear, bind with the
first-report rule and the exclusive variant of space_map, begin / keep / done with serial, 10 s resend, leave,
session end, keyed find / put for Rivals), `RestoreWindow` (guest: join, fresh, applied, none, known budget,
open / pending of BUG-1034), `HeldQueue` (the BUG-1034 queue). The four features use it; their wire bytes are unchanged
(C6 structs and golden bytes, BUG-1133). Done behind `coop.fix space.kept_store` (lab run with it on), then the four hand
paths and the switch were deleted (no new switch remains). docs/01-architecture.md#arch-space-kept-store.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
