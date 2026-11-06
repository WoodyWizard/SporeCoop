---
id: BUG-0624
title: Civ: a city the host founds instantly (campaign shortcut) and its vehicles appear on the guest 4-7 s later (sync-check v missing at low fps)
status: open
kind: desync
stage: Civ
reported: 2026-11-06
source: lab rel/0.7.9 20261106-105201 civ_city_layout (sync-check gate), INT3 reruns 20261106-122407
role: guest
versions: 0.7.9 1e155b53
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
