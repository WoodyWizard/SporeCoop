---
id: BUG-1047
title: class: module re-entrancy flags (g_applying, g_playing) set around a game call stay set after a fault the game catches: the relay of that module is off for the rest of the session
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-16
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-guard
commits: 03edbe89 530f1cd2
tests: call_guard
signature:
docs: docs/06-testing.md#testing-call-guards
related: BUG-0882, BUG-0110, BUG-0966, BUG-1074
---

## Report
Audit SPC-16 (wider than the audit says). Not seen at runtime, found by code reading.
Several modules set a plain bool around a call into the game ("we are applying a partner's change, do not send it back" / "our own call, do not relay"). A fault inside that call that the game's (or SporeKit's) handler catches unwinds past the reset, the flag stays true, and that module's relay or filter is off for the rest of the session. What a player could see after one caught fault: Space ship hits no longer relayed or filtered (squadron), Space diplomacy changes no longer sent, terraforming no longer sent, a guest's mirror sounds (mirror_fx) wrong, Civ changes no longer sent.

## Root cause
Observed in code (main efd2e0ba), raw `flag = true; <game call>; flag = false;` with no ledger entry and no per-frame reset:
* squadron.cpp:727-734 (destroy_mirror), 1115-1118 (MSG_UFO_HIT -> o_TakeDamage), 1134-1137 (MSG_UFO_DAMAGE -> h_TakeDamage); the flag gates the hook at 240.
* space_diplomacy.cpp:621-640 (inbox_task replays), 1008-1022; gates 346, 371, 394, 408.
* mirror_fx.cpp:248-256 (`g_playing` around o_Voice / o_Sprint); gates 169, 186.
* spacecoop.cpp:1130-1141, 1366, 1381 (terraform / sculpt / colour apply); gates 1036, 1202, 1211, 1250, 1309.
* civ.cpp: 19 places (902, 1762, 2403, 3651, 3667, 3749, 3840, 3906, 3927, 3973, 3982, 4002, 4462, 4479, 4526, 4647, 5793, 5882, 6073), adapters.cpp:304 `g_in_call`.
* BUG-0882 (game.cpp:1109-1124 `reset_call_guards`, run from `swap_guard_tick` once per frame) resets only game.cpp's own guards (g_in_puppet_anim, g_in_relay, t_in_anim_fx, t_in_evt).
Observed at runtime (lab 20261113-075219, the fault injected): with the guard left set, the guest's own first contact
with an empire (the game's contact call through space_diplomacy's hook) was never sent; the host's copy never met
it. Observed too: the old code's next guarded call clears the flag again (`flag = false` at its end), so "for the
rest of the session" means "until the module's next replay of a partner's change": in that run a later snapshot
replay cleared it, and everything the player did in between was lost. Found while converting (same class, not in
the audit): space_diplomacy's hook depth counter `g_depth` (gates every relay), spacecoop's `g_tool_hit_depth`,
game.cpp's `g_idle_depth` (behaviour animations reported as idle), cellsync (guest attack, death replay, own destroy,
food, knock-out death), civ `g_in_guest_fire` / `g_sw_bypass`, drama `g_in_summon_for`, player_rules (air hit,
carry relay), tribe (theme and party sound relays), coop `g_vote_bypass`. Not the same bug: adapters.cpp `g_in_call`
(its own worker thread; the vectored handler that catches the fault clears it, BUG-0286). Not covered: spacecoop's
thread_local planet seeding guards (BUG-1074).
Inferred: that each of these game calls can fault and be caught (BUG-0110 / BUG-0966 show caught faults inside game calls on players' PCs). Confidence: high for the pattern, low-medium for how often it happens.

## Fix idea
One shared mechanism: `game::CallGuard` (an RAII-free token: `size_t m = game::guard_set(&flag)` / `game::guard_clear(m)`) or a registry `game::register_call_guard(bool*)` whose flags `reset_call_guards` clears once per frame outside every game call (the BUG-0882 rule: these flags are false outside game calls). Convert the modules above in one change; flags set on purpose across frames must not be registered. Switch: under `game.lend_ledger` or its own `game.module_guards`.

## How to test
Extend the BUG-0882 test (the swap-ledger fault command `debug_swap_ledger_fault` style): a test command sets each registered flag as a fault would leave it (`coop.fix` test hook `coop.guard stick squadron`), then the module's relay must work again within 2 frames: e.g. in `coop_space_shots.py` / a Space hit test, the guest's hit on a host mirror reaches the host after the stick. Main (no reset): the hit is never relayed (FAIL).

## Fix
mods/coop/call_guard.h: one registry of the modules' call guards (`CALL_GUARD(name, bool)`,
`CALL_GUARD_DEPTH(name, int)`) and RAII holds (`callguard::Hold`, `callguard::Depth`) that replace every bare
`flag = true; call; flag = false;` (and `depth++ / --`) around a game call: 55 sites in squadron, space_diplomacy,
mirror_fx, spacecoop, civ, cellsync, drama, player_rules, tribe, coop and game. game.cpp `reset_call_guards` (the
frame's swap guard tick of BUG-0882, outside every game call) calls `callguard::reset`, which clears every registered
guard still set. The game's SEH unwind does not run the holds' destructors, so the frame reset is the fix; the holds
make each site one line and clear the guard on every early return. `coop.guard` lists the guards (`unwind <name>`,
`stick <name>` for tests). Switch: `coop.fix core.call_guard_reset off` (no reset: the 0.7.13 behaviour).

## Verification
Test `call_guard` (tools/tests/coop_call_guard.py, space_empire fixture): the guest's replay of the host's gift is
cut short like a caught fault (`coop.guard unwind space_diplomacy.applying`), then the guest's game makes first
contact with an unmet empire; the host's game must have met it (its relation flags). The fault is injected (the
destructor skipped), not made by the game, as BUG-0882's and BUG-0110's tests do.
* FAIL (SPORE_FIX_OFF=core.call_guard_reset): out/lab/20261113-075219 (guard set=1 after the replay, host flags empty).
* PASS: out/lab/20261113-075219-2, out/lab/20261113-075330 (guard cleared=1 within a second, host flags=16).
* Unit test: test_call_guard in tests/npc_tests.cpp (make test).
* Regression: out/lab/20261113-075500-2 (space_diplomacy, space_shots, space_terra_tool), out/lab/20261113-075500
  (play_civ and play_cell --quick, civ_sw_vote): all passed.
* Not run in the lab: mirror_fx, tribe, drama, player_rules, coop vote sites (mechanical conversion, covered by
  make and the unit test only).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-guard (core.call_guard_reset).
