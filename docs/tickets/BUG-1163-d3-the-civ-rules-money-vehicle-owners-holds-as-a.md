---
id: BUG-1163
title: D3: the Civ rules (money, vehicle owners, holds) as a portable header civ_logic.h with unit tests
status: open
kind: tooling
stage: Civ
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch: feat/w51-civ
commits: 84d96518
tests: tests/civ_tests.cpp
signature:
docs: docs/01-architecture.md#arch-civ-registry
related: BUG-0031, BUG-0151, BUG-0493, BUG-0613, BUG-0807, BUG-0951, BUG-1140
---

## Report
Plan item D3 of docs/plan-0.7.15.md for Civ (audit STG-8, TST-9): the Civ rules were inline decisions in civ.cpp with
no unit test (the shared money's guest income and price checks, who owns a host vehicle, what a removal waits for).

## Root cause
Structure: the rules were mixed with the game reads, so only lab tests could check them.

## Fix
mods/coop/civ_logic.h (portable, no game call): money (`guest_income_blocked`, `can_pay` BUG-0613, `refund_amount`
BUG-0807), owners (`new_vehicle_owner`, `orphan_owner` BUG-0031, `owns`), holds (`hold_removal` BUG-0493,
`unseen_mirror` / `kill_unseen` BUG-0951, `pump_waits` / `pump_kills`, `local_play` BUG-0151). civ.cpp calls them where
it decided inline; the game reads stay in civ.cpp with their short-circuits (same behaviour, no switch: a refactoring).
The held-command settle is not here: C3 (feat/w51-core2, BUG-1142) moves it into busy_model.h's HeldPump.

## Verification
tests/civ_tests.cpp in `make test` (unit-cpp, ASan / UBSan). Mutations fail it: the orphan rule without the "is here"
check, `can_pay` always true, the refund without the NaN check, `unseen_mirror` deferring a dead vehicle, a vehicle of
the shared capital kept as kShared (each: civ_tests 2 FAILED). The Civ lab tests with civ.cpp calling the rules: see
BUG-1140.
- no-fail-run: refactoring of existing rules (no behaviour change); the FAIL side is the mutations above.

## History
- 2026-11-15: opened.
