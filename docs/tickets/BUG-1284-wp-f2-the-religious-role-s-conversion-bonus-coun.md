---
id: BUG-1284
title: WP-F2: the religious role's conversion bonus counts economic vehicles of any nation (fork BUG-9078)
status: open
kind: bug
stage: Civ
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9078
role: guest
versions: 0.7.15.2
fixed_in:
branch: fix/w56-sync
commits: bc78d7e3
tests: civ_religious_bonus, civ_religious_bonus_off
signature:
docs: docs/re/civ.md#civ-influence-relay
related: BUG-0064, BUG-1053
---

## Report
Found by the Lineages fork (PR #196), its BUG-9078 (row 11): the religious governor's conversion bonus (civ.cpp
h_Convert, x1.5 / x2 with a siege) looked for a governor vehicle of purpose 2 within 200 m. Purposes are 0 military,
1 religious (cultural), 2 economic (docs/re/civ.md section 1): a trader gave the bonus, the preachers never did; any
nation's vehicle counted.

## Root cause
Observed (code): `F<int>(x.v, kVPurpose) == 2`, no political id check.

## Fix
`coop.fix civ.religious_attribution` (civ.cpp): purpose 1 and pol_of(vehicle) == the converting pol. Off: the old test.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
Pending: written and registered, not run yet (the lab waits for the orchestrator's "lab ok" in this batch: the CPU is
busy with another task). Runs to do: `civ_religious_bonus` (must PASS) and `civ_religious_bonus_off` (twin, must FAIL).

## History
- 2026-11-19: opened.
- 2026-11-19: fix and tests on fix/w56-sync (switch on); status stays open until the lab runs (FAIL twin, PASS).
