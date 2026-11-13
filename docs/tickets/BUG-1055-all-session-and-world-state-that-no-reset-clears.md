---
id: BUG-1055
title: All: session and world state that no reset clears (Tribe target ring and tribe map, Creature nest bindings, social queues, puppet health table)
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-4 WLD-5 WLD-23
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-life
commits: f2f013ec, a49f379c, ade567a3, a7f75467, 398a5ab1
tests: lifecycle_reset, lifecycle_reset_cell
signature:
docs: docs/01-architecture.md#arch-lifecycle-registry, docs/06-testing.md#testing-lifecycle-reset
related: BUG-0427, BUG-0422, BUG-0951, BUG-1051, BUG-1054
---

## Report
Audit 2026-11-12 for 0.7.14, findings WLD-4 (the ring's reset part), WLD-5 (the tribe map part) and WLD-23 (nest,
social, creature_hp). Not seen at runtime, found by code reading. Module state that belongs to a session or a world
and that no reset clears. Each is guarded today (a serial, an epoch, a list check, or harmless), so no player
symptom is known; this ticket is the inventory for the lifecycle reset package of 0.7.14 (one registry like
player_life's PLAYER_STATE, so a module added later cannot miss the reset), not a list of crashes.

## Root cause
Observed in code (main efd2e0ba), confidence high for "not cleared", the effects inferred:
* tribe.cpp:711 `g_ring` (target, ids, since, serial): not cleared by tribe::on_session_end (5240) or on_world_change
  (5271). Use is guarded by kept_alive (listed + serial, BUG-0951) and the ring member by mirror_ok (BUG-0955), so the
  audit's "freed t / unchecked g_gm" is fixed already; left: a ring of an order given before the host went away keeps
  being drawn in local play next to the game's own rings (h_RingsUpdate has no `!local_play()`; local orders do not
  pass ring_order) for up to 120 s.
* tribe.cpp:431 `g_local_tribe_of`: cleared on session end only (with tribe.world_map), not on a world change; its
  users check against all_tribes (tribe_of_index, BUG-0672) except the test path frame_tool_run (tribe.cpp:5011,
  tribe_pos on an unchecked pointer, `coop.tribe frametool`).
* nest.cpp:179 `g_own` (host egg id -> own family egg): not cleared by nest::on_session_end (nest.cpp:680, only
  clear_mirrors) or on_world_change (nest.cpp:682); cleared by guest_tick on a world epoch change (nest.cpp:411) and
  checked by object_alive + serial (nest.cpp:446). A reconnect into the same world epoch keeps bindings of the old
  host's egg ids.
* social.cpp:544 `g_meter_tasks` and social.cpp:767 `g_pending`: not cleared by social::on_session_end (1246). Both are
  drained by an outside-frame task already queued, so a success of the old session can be paid out (apply_success)
  after the session ended, in the same frame. The meter close of BUG-0808 must stay queued.
* creature_hp.cpp `g_puppets` (puppet -> reported max / hp): never cleared (no on_session_end / on_world_change in
  creature_hp.cpp), and coop.cpp forget_puppets (coop.cpp:505) does not call creature_hp::forget_puppet. h_GetMaxHp
  (creature_hp.cpp:159) answers the kept max for any creature at that address that is a puppet again (game::is_puppet),
  until the next drive_puppet. Masked, a small leak across sessions.
Related resets kept apart: BUG-1051 (the Tribe cycle tracker), BUG-1054 (the host's Tribe reload flag). Not bugs (see
the plan's audit table): civ::on_session_end's clear_coop_tables under g_fix_boost (that is BUG-0040's own switch;
on by default) and g_exist_frame (rebuilt per frame; clear_coop_tables(true) resets it in Civ).

## Fix idea
A LIFECYCLE registry next to player_life.h: `SESSION_STATE("tribe.ring", reset, held)` and
`WORLD_STATE(...)` entries called from coop.cpp's session end and world change, each module registering its globals
next to their definition; `coop.lifecycle held` lists what is still held after a reset (tests). Move the entries above
into it (and the ones the other 0.7.14 audit tickets list). Switch: `coop.fix core.lifecycle_reset off`.

## How to test
One general twoinst test for the registry (e.g. tools/tests/coop_lifecycle_reset.py): play a short session per stage
(Creature, Tribe, Civ, Space fixtures), end it (Leave on the guest, host quits to the menu), then ask
`coop.lifecycle held` on both games: every registered entry 0. Main (no registry, the fix off): the entries above are
held: FAIL. Per item where a player effect exists: the Tribe ring in local play (the guest orders an attack, the host
goes to the menu, assert TribeTargetRing_Find has no ring for the old target after the guest's own new order).

## Fix
One registry (mods/coop/lifecycle.h, owner decision 3): `SESSION_STATE` / `WORLD_STATE` / `LIFECYCLE_STATE(name,
scope, fix, reset, held)` next to each module's globals; coop.cpp calls `lifecycle::reset(kSession)` at the end of
end_session (block 3, every path) and `lifecycle::reset(kWorld)` once a frame when the sum of the stage counters
(stage_epoch.h) moves. Right after a reset the registry keeps what each entry of that scope still holds;
`coop.lifecycle held` prints it (`left session: none`, `left world: none`) and what is held now. The items of this
ticket: tribe.ring (session + world), tribe.world_map (world), nest.own (session), social.meter_tasks (session; the
close of BUG-0808 stays queued) and social.pending (session), creature_hp.puppets (session + world; coop.cpp
forget_puppets also forgets the puppet's reported health now). Switch `coop.fix core.lifecycle_reset off`: no entry
is reset (each module's own resets only, 0.7.13). Not changed: the ring is still drawn for the guest's own order in
local play (it is the guest's order on its own selected member; at most 120 s, and only while the target lives).

## Verification
Test `lifecycle_reset` (coop_lifecycle_reset.py: Creature, Cell, Tribe, Civ, Space on the same two games; the
host loads each fixture with coop.load, the guest joins and loads it, 20 s of play, the session ends by Leave or by a
drop; both games' last session / world reset must leave nothing).
FAIL (SPORE_FIX_OFF=core.lifecycle_reset): out/lab/20261113-011601 (cell.join, cell.own_good, nest.own, mirror_fx.keys
left at the session ends and world changes). PASS: out/lab/20261113-011223 (old stage order), out/lab/20261113-012450-2, out/lab/20261113-013333.
The first run out/lab/20261113-010644-2 passed every check and failed on the monitor's puppet_hp gate after Cell ->
Creature (also with the switch off: BUG-1061, a separate ticket; the test now plays Creature first).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-life (package L1, the lifecycle registry of BUG-1055).
