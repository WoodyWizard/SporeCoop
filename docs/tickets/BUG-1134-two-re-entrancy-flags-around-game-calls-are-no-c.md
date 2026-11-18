---
id: BUG-1134
title: Two re-entrancy flags around game calls are no CALL_GUARD: game.cpp g_in_puppet_anim and the effect relay's g_in_relay
status: fixed
kind: bug
stage: All
reported: 2026-11-15
source: B4 lint
role: 
versions: 
fixed_in: 0.7.15
branch: fix/w51-guards2
commits: 50b79778 2e60640b
tests: call_guard_game
signature:
docs: docs/06-testing.md#testing-call-guards, docs/archive/known-issues-2026.md#issue-game-unregistered-guards
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

CORRECTION (2026-11-15, fix/w51-guards2): "for the rest of the session" was not true. BUG-0882 (0.7.13,
be40ef44) clears both flags (with the thread_local t_in_anim_fx / t_in_evt) once a frame in game.cpp
`reset_call_guards` under `game.lend_ledger` (npc::tick -> swap_guard_tick runs every frame in every stage): a left
flag lasted at most until the next frame. The real gap was that the two were outside the registry (no `coop.guard`
entry, no `core.call_guard_reset`, the lint's baseline). Also not as the report says: a stuck `g_in_relay` does not
stop every effect hook; only the world effect hooks (h_MgrCreateFx, h_WorldCreateFx) and the animation event effect
(anim_event_effect) read it; the creature pool hooks (pools 1-3) never did (seen in the FAIL run: the host's creature
effects were still sent). A stuck `g_in_puppet_anim` stops the own avatar's animation relay (h_PlayAnim /
h_PlayAnimTo -> on_anim, MSG_ANIM) and the behaviour animation report (bhv_report, NPC relay).
Also observed (lab 20261115-025041): one mating call of the guest makes about 5 plays of a partner's animation on the
host (the call's three animations 0265D18B/C/D and more) and one effect play (pool 3).

## Fix idea (B4)
Not done on feat/w51-guards (B4 only baselines them; a fix is a behaviour change with its own FAIL/PASS twin):
register both (`CALL_GUARD("game.puppet_anim", g_in_puppet_anim)`, `CALL_GUARD("game.fx_relay", g_in_relay)`),
hold them with `callguard::Hold`, and test with `coop.guard unwind game.fx_relay` / `game.puppet_anim` (the effect
relay / behaviour anims still reach the partner after the cut-off call), twin `SPORE_FIX_OFF=core.call_guard_reset`.
Then drop the two entries from tools/ratchet_baseline.json (`ratchet_lint.py --lower`).

## Fix
mods/coop/game.cpp (50b79778): `CALL_GUARD("game.puppet_anim", g_in_puppet_anim)` and
`CALL_GUARD("game.fx_relay", g_in_relay)`; play_animation, play_behaviour_anim, create_effect_at (Hold with
`relayed`), create_creature_effect and start_effect hold them with `callguard::Hold` (the old `bool was` restore is
gone: outside a game call the flag is false, so ending false is the same; nothing of ours calls these inside such a
call). `reset_call_guards` no longer clears the two itself (callguard::reset does, switch `core.call_guard_reset`);
`game.lend_ledger` keeps clearing the two thread_local guards. tools/ratchet_baseline.json: unregistered guards 4 -> 0.
Switch: `coop.fix core.call_guard_reset off` (the guards stay set, as before BUG-0882). Wire bytes, the `coop.fix`
list and its texts unchanged; `coop.guard` lists two more guards. Not player-visible (with the default switches the
flags were already cleared once a frame), no CHANGELOG line.

## Verification
Test `call_guard_game` (tools/tests/coop_call_guard_game.py, creature fixture): the host arms both guards
(`coop.guard unwind game.puppet_anim 10`, `game.fx_relay 5`), the guest's mating call cuts the host's plays of its
animation and effect short; then the host's own mating call must play on the guest's puppet of the host (the guest
game's current animation, `coop.npc cur`) and the host's part found (world effect, pool 5) must be active in the
guest's game. The fault is injected (the hold's destructor skipped), as in BUG-1047's test.
* FAIL (SPORE_FIX_OFF=core.call_guard_reset): out/lab/20261115-025220 (both guards set=1 1.5 s later; the guest's
  puppet kept 02481DE5 through three host mating calls 0265D18B/C/D; no pool 5 effect reached the guest, host sent
  stayed 4).
* PASS: out/lab/20261115-025412, out/lab/20261115-025526 (cleared=1 each within 1.5 s, the guest's puppet played 0265D18B, the
  world effect A3C5EB2B active 1.5 m from the host's puppet). First run with ARMED 5: out/lab/20261115-025041 (PASS).
* make test (unit-lint: ratchet_lint 0 unregistered guards), play_creature --quick: out/lab/20261115-025652 (PASS).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on fix/w51-guards2 (registered call guards, core.call_guard_reset; CORRECTION: BUG-0882 already cleared both once a frame).
- 2026-11-18: released in 0.7.15.
