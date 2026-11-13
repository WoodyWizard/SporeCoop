---
id: BUG-1047
title: class: module re-entrancy flags (g_applying, g_playing) set around a game call stay set after a fault the game catches: the relay of that module is off for the rest of the session
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-16
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0882, BUG-0110, BUG-0966
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
Inferred: that each of these game calls can fault and be caught (BUG-0110 / BUG-0966 show caught faults inside game calls on players' PCs). Confidence: high for the pattern, low-medium for how often it happens.

## Fix idea
One shared mechanism: `game::CallGuard` (an RAII-free token: `size_t m = game::guard_set(&flag)` / `game::guard_clear(m)`) or a registry `game::register_call_guard(bool*)` whose flags `reset_call_guards` clears once per frame outside every game call (the BUG-0882 rule: these flags are false outside game calls). Convert the modules above in one change; flags set on purpose across frames must not be registered. Switch: under `game.lend_ledger` or its own `game.module_guards`.

## How to test
Extend the BUG-0882 test (the swap-ledger fault command `debug_swap_ledger_fault` style): a test command sets each registered flag as a fault would leave it (`coop.fix` test hook `coop.guard stick squadron`), then the module's relay must work again within 2 frames: e.g. in `coop_space_shots.py` / a Space hit test, the guest's hit on a host mirror reaches the host after the stick. Main (no reset): the hit is never relayed (FAIL).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
