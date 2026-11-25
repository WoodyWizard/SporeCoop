---
id: BUG-1555
title: Civ & Space Rivals: NPC diplomacy wipe, guest SpendMoney, Civ->Space liftoff stranding, city handoff, Space influence price and NPC UI
status: open
kind: bug
stage: Civ
reported: 2026-11-25
source: v0.8.0 independent audit
role: both
versions: 0.8.0
fixed_in:
branch: w0801/civ-space
commits:
tests:
signature:
docs: docs/11-rivals.md
related: BUG-1549, BUG-1550
---

## Report
Independent static audit of `BUG-1549` and `BUG-1550` found critical gameplay blockers:
1. `civ_rivals.cpp:710-738` overwrites `cRelationshipManager` from `g_npc_rels` every 1.0 s without reading back native diplomacy changes.
2. `civ_host.cpp:1086,1107` calls `a_SpendMoney` on guest civs with `mIsPlayer==0`, refilling NPC budgets instead of deducting money.
3. Tribe->Civ and Civ->Space draft payouts are never delivered on stage entry.
4. Natural `Civ -> Space` start defers `relinquish_host_home_now` until liftoff (`space_rivals.cpp:828-831`), but post-liftoff retry hits early return (`857-862`), leaving guest stranded at host's star instead of traveling to its new homeworld.
5. `set_guest_civ_cities` (`space_rivals.cpp:146`) is never called by `civ_rivals` on transition.
6. `on_buy_influence_space` (`space_rivals.cpp:1011-1039`) charges raw 50/75/100 Sporebucks instead of Space-scaled money.
7. `space_rivalry.cpp:511,1119,1292` toggle desyncs from `rivals::config()`, and `sync_from_rivals()` drops sync when `money_ptr()` is null.
8. Civ and Space lack `on_npc_ui_list` providers for F9 Influence UI.

## Root cause
See `docs/plan-0.8.0.1.md` WP-5.

## Fix
TBD.

## Verification
TBD.
