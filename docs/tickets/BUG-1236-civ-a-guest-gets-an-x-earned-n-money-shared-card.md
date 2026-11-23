---
id: BUG-1236
title: Civ: a guest gets an 'X earned N money (shared)' card every 6 s through the whole stage (the cities' income ticks of 40-380 pass kCauseMin 25)
status: open
kind: bug
stage: Civ
reported: 2026-11-18
source: key-frame review of final check 20261117-154911
role: 
versions: 0.7.10, 0.7.14.1, unreleased
fixed_in:
branch: feat/w68-wallet
commits: 162b29f0 ebe0e45a 86ef0e48
tests: tests/civ_tests.cpp (passive_income_cause), tools/tests/coop_civ_income_cause.py (civ_income_cause, twin civ_income_cause_off)
signature:
docs: docs/known-issues.md#issue-civ-money-cards
related: BUG-0696, BUG-1468
---

## Report
Key-frame review of the second 0.7.15 final check (out/frames-0.7.15-b.md, finding 2): guests get the card every 6 s through the Civ stage, 698 lines in the guests' logs of lab 20261117-154911 (632 in 20261116-025154). mods/coop/shared.cpp (BUG-0696) says the cities' income stays under kCauseMin = 25; the income ticks are 40-380.

## Root cause
shared.cpp's host cause (BUG-0696) is `v - synced - applied`: everything the host's game changed outside the guests'
applied changes. The game's passive income (the 6 s city and spice derrick tick, AddMoney called at 0xBF7361; the
automatic trade deliveries of cCity::TradeDelivery, 0xBE1A21 / 0xBE1C22; objdump-checked, docs/re/civ.md
#civ-income-addmoney) is 40-380 a tick, over kCauseMin 25: every guest got "<host> earned N money (shared)" every 6 s.

## Fix
Ported from fix/agy-open-bugs 13148efe (the outside agent's commit; review out/research/agy-review-misc.md: right, the twin
was missing) on our code: civ.cpp h_AddMoney recognises the three return addresses (`SporeCoop::CivIncome_AddMoney_Ret`,
`CivTrade_CityAddMoney_Ret`, `CivTrade_TraderAddMoney_Ret`, .sym rechecked), measures the gain on +0x98 and tells
shared::note_passive(2, gain), which net::host_own_cause takes out of the host's cause; the total still carries it. The
players' city trade (civ_host.cpp trade_tick, its own notice every 6th payout) is noted too (review nit 10). With
civ.own_money the same classifier splits the income (BUG-1468): what stays with the host is noted. Switch renamed per
the review: `coop.fix civ.income_cause` (off: 0.7.16.1). Status: shared passive=total/ticks, coop.civ money:
passive_noted=.

## Verification
Unit: tests/civ_tests.cpp `passive_income_cause` (net::host_own_cause with a passive tick alone, with a host purchase,
a sale, a guest's change, two ticks, own money's split, the cap). Lab: pending night run: civ_income_cause (24 s idle:
no "earned" card on the guest; the host's -300 still gives "spent 300") and twin civ_income_cause_off (a card every
6 s: must FAIL); regressions res_cause, res_cause_floor, civ_coop.

## History
- 2026-11-18: opened from the key-frame review; old class, left for the next release.
- 2026-11-23: implemented on feat/w68-wallet (commits 162b29f0, ebe0e45a and after); unit tests pass; lab tests and twins written for the night run.
