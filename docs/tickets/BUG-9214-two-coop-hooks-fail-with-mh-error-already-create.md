---
id: BUG-9214
title: Two coop hooks fail with MH_ERROR_ALREADY_CREATED: stale stimulus guard and civ vehicle cosmetic guard off
status: fixed
kind: bug
stage: Civ
reported: 2026-11-15
source: overnight stage tour 2026-11-15 (log)
role: 
versions: 0.7.8-lineages53,0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 9c2efe20
tests: tools/qa_win (manual, 2026-11-15): the game log at start (TITAN and the MSI)
signature:
docs:
related:
---

## Report
Seen in the overnight stage tour's logs: every start writes two `hook_create ... failed: MH_ERROR_ALREADY_CREATED` errors (the owner's Lineages 53 log too).

## Root cause
Two modules hook the same game function each time (MinHook allows one): cGameNounManager::DestroyInstance (civ.cpp BUG-0539 before gamefixes' BUG-0456 guard) and the Civ vehicle TakeDamage 0x00CAB6F0 (two symbol names). The second hook of each never installed.

## Fix
- MinHook allows one hook per function. civ.cpp hooked cGameNounManager::DestroyInstance (BUG-0539) before gamefixes did, so the stale stimulus sender guard (BUG-0456, a crash guard) was off ("stale stimulus sender guard off" in the log) in Lineages 53 and 54-dev. Ported upstream's BUG-0623 fix (64f66aea): gamefixes owns the hook, civ.cpp listens.
- The Civ vehicle TakeDamage (0x00CAB6F0) was hooked twice too (SporeCoop::CivVehicle_TakeDamage and Simulator::cVehicle::TakeDamage): the BUG-0477 cosmetic shot guard now lives in the hook that installs (civ.vehicle_cosmetic).

## Verification
FAIL (before 9c2efe20): two "hook_create ... failed: MH_ERROR_ALREADY_CREATED" lines at every start (the owner's Lineages 53 log too) and "game fixes: stale stimulus sender guard off".
PASS (9c2efe20, TITAN and the MSI): no such line; "stale stimulus sender guard on".
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-15: opened.
- 2026-11-15 (overnight tour): found in the stage tour's logs; fixed (9c2efe20).
