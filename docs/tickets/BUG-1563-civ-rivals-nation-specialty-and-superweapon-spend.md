---
id: BUG-1563
title: Civ Rivals: guaranteed nation/city specialty conversion to match Tribe card and guest superweapon spending
status: open
kind: feature
stage: Civ
reported: 2026-11-25
source: 0.8.1 Rivals isolation audit
role: both
versions: 0.8.0.2
fixed_in:
branch: w081/civ-spec-superweapon
commits:
tests: rivals_civ_spec_sw, rivals_civ_spec_sw_off
signature:
docs: docs/11-rivals.md
related: BUG-1549, BUG-1555, BUG-1557
---

## Report
1. In Civ Rivals, if all players finished Tribe with the same consequence card (or no free NPC nation on the planet happens to have the matching Military/Economic/Religious specialty), a guest is assigned an NPC nation with a mismatched specialty instead of converting that taken-over nation/capital to match the guest's own Tribe consequence card.
2. When a guest fires a superweapon in Civ Rivals (`civ.cpp` `host_superweapon_request`), `o_FireSW(pc, type, pos)` runs against a guest `cCivilization` with `mIsPlayer == 0` where native `SpendMoney` fails/refills NPC budgets instead of deducting from `mWealth (+0x98)` (same class as BUG-1555).

## Root cause
- `civ_rivals.cpp` prefers matching NPC nations on takeover, but falls back to an unmatching NPC nation without updating its specialty and capital city specialty (`cCity +0x29C`).
- `civ.cpp` `host_superweapon_request` calls `o_FireSW` directly in Civ Rivals without checking/deducting cost via `civ_rivals::host_spend_civ_money`.

## Fix
- Convert a taken-over guest NPC nation and its capital city specialty to match the guest's Tribe consequence card when no naturally matching free NPC exists (behind `COOP_FIX("rivals.civ_spec_convert", "BUG-1563")`), ensuring `civ_arrival.cpp` opens the matching vehicle editor and `history` records the true starting specialty.
- Validate and deduct guest superweapon costs directly via `civ_rivals::host_spend_civ_money` (avoiding `SpendMoney` on `mIsPlayer == 0`) behind `COOP_FIX("rivals.civ_sw_spend", "BUG-1563")`.

## Verification
Pending implementation and Wine lab ON/OFF twin verification.
