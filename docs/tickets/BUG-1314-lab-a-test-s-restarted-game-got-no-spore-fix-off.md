---
id: BUG-1314
title: lab: a test's restarted game got no SPORE_FIX_OFF (coop.fix unknown right after its start): twins ran with the fix on
status: fixed
kind: test
stage: Tools
reported: 2026-11-19
source: lab
role: 
versions: 0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w57-tribe-keys
commits: 4ce6f9de
tests: space_own_ship_key_off, own_editor_key_off, late_species_off (twins that restart the guest)
signature: SPORE_FIX_OFF (guest, restarted): unknown command: coop.fix (try 'help')
docs: docs/06-testing.md
related: BUG-1301, BUG-1297, BUG-1289, BUG-0214
---

## Report
Lab 20261119-105127: space_own_ship_key_off PASSED although it must FAIL. Its log: `SPORE_FIX_OFF (guest, restarted):
unknown command: coop.fix (try 'help')` and the guest's `coop.creations` said `own ship key: fix=1`.

## Root cause
✔ twoinst.apply_fix_off sent `coop.fix <name> off` once, right after wait_up (SporeKit's console answers `ping`
before the mod has registered its commands) and only printed the answer: the restarted guest kept the fix on and the
twin tested the fixed code. Every test that restarts a game and calls apply_fix_off had this (own_editors_robust's
key / ship modes, late_species, tribe_own_species).

## Fix
apply_fix_off asks again every second until the answer says the switch is off (at most 90 s) and fails the test
(RESULT: FAIL, as start() does for an unknown switch) when it never does.

## Verification
no-fail-run: test harness; shown by the twins: no-fail-run: test harness. The twins it repairs now run with the switch off on the restarted guest (the log names the
switch, `own ship key: fix=0`) and FAIL: space_own_ship_key_off 20261119-115330, own_editor_key_off 20261119-112300,
late_species_off 20261119-112300.

## History
- 2026-11-19: opened (fix/w57-tribe-keys).
- 2026-11-19: fixed on fix/w57-tribe-keys.
- 2026-11-23: released in 0.7.16.
