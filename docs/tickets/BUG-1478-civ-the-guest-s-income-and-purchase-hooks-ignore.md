---
id: BUG-1478
title: Civ: the guest's income and purchase hooks ignore "guest elsewhere" (they gate on the Civ mode only, not on local play) (CIV-5)
status: open
kind: desync
stage: Civ
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: feat/w68-wallet
commits: 162b29f0 ebe0e45a
tests: tests/civ_tests.cpp (local_play_gate), tools/tests/coop_civ_other_world.py --income (civ_other_world_income, twin civ_other_world_income_off)
signature:
docs: docs/re/civ.md#civ-own-money
related: BUG-1236, BUG-1468, BUG-0231
---

## Report
External static audit of 0.7.15.5, item CIV-5 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-M1 (feat/w68-wallet). A behaviour change gets a switch and a twin.

## Root cause
Static (the audit's reading, confirmed on rel/0.7.17): `local_play()` is host away OR guest elsewhere, but the guest
hooks of civ.cpp gated on `is_guest() && mode == kCiv` only: h_AddMoney dropped every income of a guest's own Civ
world while it was in a session from another world (BUG-0231 state), h_CreateBuilding / h_SpawnNextCity / h_SpawnVehicle
blocked its own NPC growth, founding and AI vehicles, h_Capture refused its own captures (and sent the index of its own
world's city to the host as CMD_CAPTURE while "away"), h_CityBuy sent its purchases to the host, h_FireSW sent CMD_SUPER
and the host's host_superweapon_request fired the host's superweapon at that point of the other planet.

## Fix
`coop.fix civ.local_play_gate` (on): civlogic::guest_holds(gate, guest, in_civ, own_world::elsewhere()): the seven hooks
hold only while the guest plays the host's Civ world; a guest elsewhere runs its own game (the host away is not
elsewhere: BUG-0151 / BUG-0878 stay). The host drops kinds 5 (superweapon), 6 (city purchase), 7 (capture) from a
guest `own_world::peer_elsewhere(from)` (civlogic::host_takes_world_cmd; `coop.civ` money: world_cmds_dropped=). Off:
0.7.16.1.

## Verification
Unit: tests/civ_tests.cpp `local_play_gate`. Lab: pending night run: civ_other_world_income (PASS: the guest's own
world earns its cities' income in the session), twin civ_other_world_income_off (SPORE_FIX_OFF=civ.local_play_gate:
income dropped, must FAIL); regressions civ_other_world, civ_away, civ_away_capture, civ_away_buy, civ_ai_spawn.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-M1), branch feat/w68-wallet.
- 2026-11-23: implemented on feat/w68-wallet (commits 162b29f0, ebe0e45a and after); unit tests pass; lab tests and twins written for the night run.
