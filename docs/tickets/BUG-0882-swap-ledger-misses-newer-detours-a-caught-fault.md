---
id: BUG-0882
title: Swap ledger misses newer detours: a caught fault in the relevance pass leaves the game's sphere vector on our static buffer, one in the creature scale call leaves the player-species override set
status: fixed
kind: crash
stage: Creature
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-freed
commits: 1e58b7e0
tests: lend_ledger
signature:
docs: docs/06-testing.md#testing-freed-block-reuse
related: BUG-0110, BUG-0280, BUG-0800
---

## Report
BUG-0110's premise holds: the game's own handler catches faults inside a call (logs show thousands of caught faults per frame), so code that changes game state before calling the original and restores it only on a normal return stays changed for good. The BUG-0110 ledger (`game::swap_open` / `swap_close`, undone once per frame by `swap_guard_tick`) covers only NPCTickAI, NoAttack, SelectUI, StageMsg and civ `b_damage`. Later detours do the same outside the ledger:
* With a partner in Creature or Tribe on the host, the relevance pass runs every frame; a fault inside it leaves the game's sphere vector (mgr+0x20/+0x24/+0x28) on our static array, which the game's vector code can later free or reallocate: heap corruption and a crash far away on the host.
* A fault inside the creature scale call leaves the player-species override set: every PlayerSpecies call on that machine then returns another creature's species (relations and scale wrong for the rest of the session).

Severity: medium. Confidence: medium.

## Root cause
Observed with the unwind injection (fix/w48-freed, 2026-11-12): after a relevance pass with partners that left like a caught fault (`coop.views unwind`), the game came into every later pass with its sphere vector on the mod's static buffer (`coop.views`: on_our_buffer=1); after the own creature's scale call left so (`coop.hp scaleunwind`), the game's PlayerSpecies answered the guest's own species for good (lab 20261112-010920). The mod-only call guards (inside a puppet's animation, an effect wrapper, an event, a relayed effect; social's goal) had the same pattern.

## Fix idea
Open a ledger entry around both calls (h_Relevance: restore v[0..2] to the game's own pointers; h_Scale: clear g_ps_override) and let swap_guard_tick undo them like the others; reset the mod-only guards in the same per-frame tick. One shared helper ("lend X for one call") closes the class; the drama.cpp swap (BUG-0800) could use it too. Twin switch: npc.swapguard already turns the per-frame undo off.

## How to test
Extend `coop.npc swapfault` (e.g. `swapfault relevance|scale`) so the next h_Relevance / h_Scale returns without its restore, the state a caught fault leaves. Host in Creature with a guest far away: after one frame the game's mgr+0x20 begin pointer must not equal g_buf (expose it in `coop.views`), the game's PlayerSpecies call must return the host's own species, and the guest's nearby nest members must stay present on the guest's game (the BUG-0280 assertion). Must FAIL with `SPORE_FIX_OFF=npc.swapguard` and PASS with the fix; no crash report on either game.

## Fix
mods/coop/view_centres.cpp: the lend of the sphere vector is a swap-ledger entry (undo: the game's pointers back while the vector is still on our buffer); mods/coop/creature_hp.cpp: the player species override is one (swap_undo_ptr_if); mods/coop/game.cpp: the frame's swap guard also clears the mod's call guards, social::tick its goal. Switch: `coop.fix game.lend_ledger off` (no entries, no reset).

## Verification
Test `lend_ledger` (tools/tests/coop_lend_ledger.py).
* FAIL (SPORE_FIX_OFF=game.lend_ledger): out/lab/20261112-010920, out/lab/20261112-013136 (with the call guards: PlayerSpecies still the guest's own species; the game's vector on the mod's buffer).
* PASS: out/lab/20261112-010824-2, out/lab/20261112-013003.

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0110. Severity medium, confidence medium.
- 2026-11-12: fixed on fix/w48-freed (game.lend_ledger); a fault inside these calls is injected, not made by the game (`coop.views unwind`, `coop.hp scaleunwind`), as BUG-0110's tests do.
