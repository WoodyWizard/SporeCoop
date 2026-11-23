---
id: BUG-1284
title: WP-F2: the religious role's conversion bonus counts economic vehicles of any nation (fork BUG-9078)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9078
role: guest
versions: 0.7.15.2
fixed_in: 0.7.16
branch: fix/w56-sync
commits: bc78d7e3, 6fdea267
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

CORRECTION (2026-11-19, fix/w57-sync, lab run 20261119-104136): the first run failed in the test's setup: the civ_npc
fixture's civilization has no economic or religious vehicle allowance (VehicleSlotsLeft 0, the allowance 0xBF2110
counts cities of that purpose; docs/re/civ.md#civ-npc-fixture-slots), `coop.civ spawnveh` made nothing. The test spawns
past it (`spawnveh ... force`) and puts its vehicles on the ground (the city's sphere). No mod change for this ticket.

## Fix
`coop.fix civ.religious_attribution` (civ.cpp): purpose 1 and pol_of(vehicle) == the converting pol. Off: the old test.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Lab, first night run (rel/0.7.16 c7699dca, rel/0.7.16 worktree): civ_religious_bonus FAIL out/lab/20261119-104136
  ("the host has an economic vehicle (None)": the setup above), its twin the same.
- Lab with the test fixed, merged tree (fix/w57-sync 3801b9f5, fix/w57-sync worktree): PASS civ_religious_bonus
  out/lab/20261119-121505 (the trader next to the city: 1 convert moves 1; the religious vehicle: 2; the guest has the
  host's followers); also 20261119-111559 and 20261119-115145 (x2). FAIL twin civ_religious_bonus_off
  (SPORE_FIX_OFF=civ.religious_attribution) out/lab/20261119-121505 for the right reason: the trader gives the bonus
  (1 convert moved 2).

## History
- 2026-11-19: opened.
- 2026-11-19: fix and tests on fix/w56-sync (switch on); status stays open until the lab runs (FAIL twin, PASS).
- 2026-11-19: fixed (fix/w57-sync): see the CORRECTION under Root cause; PASS and FAIL twin run dirs above.
- 2026-11-23: released in 0.7.16.
