---
id: BUG-0613
title: Civ: the detector finds the money apart (host -800, guest 0) in civ_coop
status: fixed
kind: desync
stage: Civ
reported: 2026-11-06
source: detector, C2 regression run (fix/w45-cell out/lab/20261106-015141, civ_coop)
role: host
versions: 0.7.9, 0.7.10
fixed_in: unreleased
branch: fix/w46-v2-civ-space
commits: dbc37d11
tests: civ_money_short, civ_coop
signature: DETECTOR Civilization:state:money "money -800 on the host, 0 on the guest" (civ_coop)
docs: docs/re/civ.md#civ-spend-below-zero, docs/06-testing.md#testing-civ-money-short
related: BUG-0106, BUG-0311
---

## Report
The divergence detector (SPORE_DETECTOR=20) reported a row `Civilization:state:money` with host -800 and guest 0 in civ_coop (smoke) on fix/w45-cell (out/lab/20261106-015141 in that worktree, civ_coop.log). Not analysed: whether the money of the guest's civ should mirror the host's, or the row is an artifact of the detector. For 0.7.10 (V2).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lab, game logs of rel/0.7.10 20261107-141346 civ_coop and lab 20261107-205315 with the fix off): the money
of the `civ` fixture is 3000; civ_coop's guest buys a vehicle (1000, host_buy checks the money) and builds a house
(1600, `coop.civ build` on the guest: paid in its own game, "shared money -1600.0 from player 1" on the host: 400
left), then the host's `coop.civ build 0 factory` (1200) went through `build_in_city(.., pay=true)`, which paid with
cCivilization::SpendMoney 0xBEF860. SpendMoney has no floor: the host's game held -800 (trace money=-800) and
broadcast it; every guest floors a total at 0 (shared.cpp money_clamp, BUG-0106): host -800, guest 0 for the rest of
the test, the detector's row in all 29 civ_coop logs that have it. Only the mod's own paid build (the console command
tests use, on either game) does this: the game's planner offers only what the money covers, a purchase (host_buy)
checks GetMoney first. Not this cause: the campaign's `Civilization:state:money` rows (host 3530, guest 3000) in
civ/advance, where every guest's game reloads the Civ world every ~22 s (mode 01654C08, 47 times in the 0.7.10 final
check's campaign) and shows the save's money for a moment; that is the stuck end of Civ (BUG-0311).

## Fix
civ.cpp `build_in_city`: a paid build reads the building's cost (vt 0x5C once SetBuildingModel set its type) and
GetMoney before the slot; short: the instance is destroyed (DestroyInstance) and the command answers "not enough
money". The guest's `coop.civ build` sends its command only after its own copy was built (and paid). civ_coop gives
the host the money for its factory first. Switch: `coop.fix civ.build_money off` (built and paid below 0, the command
sent before the guest's own copy).

## Verification
civ_money_short (`tools/tests/coop_civ_money_short.py`: shared money 500, the host's factory and the guest's house
not built, the same money on both and not below 0; then with 10000 the factory built on both and paid once):
FAIL with SPORE_FIX_OFF=civ.build_money: out/lab/20261107-205315 (host -660.0, guest 0.0; both buildings built);
PASS: out/lab/20261107-215251 (civ_money_short #1 lane 10, #2 lane 9: "not enough money", 500 on both, the factory
built and paid once with 10000); civ_coop #1, #2 in the same run passed with no `Civilization:state:money` row
(SPORE_DETECTOR=20).

## History
- 2026-11-06: opened.
- 2026-11-07 (fix/w46-v2-civ-space, 0.7.11 V2): reproduced (the mod's paid build below 0), fixed (civ.build_money).
