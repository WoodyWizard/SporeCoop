---
id: BUG-1134
title: Two re-entrancy flags around game calls are no CALL_GUARD: game.cpp g_in_puppet_anim and the effect relay's g_in_relay
status: open
kind: bug
stage: All
reported: 2026-11-15
source: B4 lint
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1047, BUG-1131, BUG-0882
---

## Report
Found by the B4 `guard` ratchet lint (tools/ratchet_lint.py, BUG-1131) on rel/0.7.15 6cbddd4e: two flags of
mods/coop/game.cpp are set around a call of an original-function trampoline and are not registered with
call_guard.h (BUG-1047's class: a fault the game catches inside the call unwinds past our frame and leaves the
flag set, the per-frame callguard::reset only clears registered guards).
* `g_in_puppet_anim` (game.cpp: set around `o_PlayAnim` in the puppet animation call and around
  `o_CreaturePlayAnim` / `o_PlayAnim` in the behaviour replay): left set, `bhv_report` never tells the behaviour
  listener again, so the local creature's behaviour animations stop being relayed for the rest of the session.
* `g_in_relay` of the effect hooks (game.cpp: `create_creature_effect`, `start_effect`, save / restore around
  `o_CreateCreatureFx` / `o_StartStopFx1/2`): left set, every effect hook returns early (`if (g_in_relay) return`),
  the effect relay stops for the session. (player_rules.cpp's own `g_in_relay` is registered: carry_relay.)
Not seen by a player or a test so far (no report names it); found by reading.

## Root cause
Inferred from the code: bare `flag = true; o_X(...); flag = false;` (or `= was`) without CALL_GUARD, the pattern
BUG-1047 converted elsewhere. Both flags are false outside a game call, so registering them is safe by the rule of
call_guard.h.

## Fix
Not done on feat/w51-guards (B4 only baselines them; a fix is a behaviour change with its own FAIL/PASS twin):
register both (`CALL_GUARD("game.puppet_anim", g_in_puppet_anim)`, `CALL_GUARD("game.fx_relay", g_in_relay)`),
hold them with `callguard::Hold`, and test with `coop.guard unwind game.fx_relay` / `game.puppet_anim` (the effect
relay / behaviour anims still reach the partner after the cut-off call), twin `SPORE_FIX_OFF=core.call_guard_reset`.
Then drop the two entries from tools/ratchet_baseline.json (`ratchet_lint.py --lower`).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
