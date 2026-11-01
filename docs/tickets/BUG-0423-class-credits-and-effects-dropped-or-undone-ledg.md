---
id: BUG-0423
title: class: credits and effects dropped or undone (ledger, ack, owed effects) (umbrella)
status: open
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
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
Fixed per case: BUG-0058, 0220 (join path only), 0225, 0226, 0228, 0233, 0239.
Siblings not covered (obs): shared.cpp MSG_RES_ADD returns before the ack when the host cannot read the value (:281 vs :326): guest-earned Civ money / Tribe food lost while the host is in the menu; have=false on a mode mismatch (:216-218, :294): a guest's spend right after an editor or load is undone (free money/food); Civ guest purchases, planner buildings, superweapons, gifts silently dropped while the host is away (civ.cpp:402-421, 455-477, 802-813; send_cmd civ.cpp:331, tribe.cpp:303); Space contract / K_MISSION tool rewards lost outside Space (space_contracts.cpp:243/523-545, spacecoop.cpp:1164-1190); contract deadlines on wall-clock time; MSG_RES (money, spice) has no cause tail (BUG-0389 class for Civ/Space).
General fix: ledgers pause, never drop; always ack; one owed-effect queue keyed by world identity; host-absent check in every command sender with feedback. Outcome check: every credit path with the receiver in menu / editor / load, game values equal on both machines.
Source: docs/generalisation-plan.md G6.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
