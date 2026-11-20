---
id: BUG-9277
title: Civ own nations: with a partner nation in the world and civ.own_money off, a guest who shares the host's nation no longer gets the shared money
status: open
kind: bug
stage: Civ
reported: 2026-11-20
source: found in the port of BUG-9274 (review, 2026-11-20)
role: 
versions: 0.7.15.5 (feat/lineages-civ)
fixed_in:
branch: feat/lineages-civ
commits: 2e429fef
tests:
signature:
docs: docs/re/civ.md#civ-own-nations
related: BUG-9274 BUG-9259
---

## Report
Found in review of BUG-9274: civ_wallet::active() is true on the host as soon as a partner nation lives (its nation's
money is shown through the wallet), and shared.cpp leaves the Civ money alone whenever civ_wallet is active. With
civ.own_money off a third player who plays the host's nation would then no longer get the shared money.

## Root cause
Inferred from the code (no three-player run on this PC: two instances per lane).

## Fix
shared.cpp own_budget: on the host the Civ money stays shared unless the setting civ.own_money is on; a guest with its
own nation ignores it on its side (its civ_wallet is active there). Switch `coop.fix civ.nation_share off`: the host
stops sharing as BUG-9274's first build did.

## Verification
no-fail-run: needs three players (two instances per lane here); checked on lane C with two that the guest with its own
nation keeps its nation's money while the host's money changes (an income on the host).

## History
- 2026-11-20: opened and fixed on feat/lineages-civ.
