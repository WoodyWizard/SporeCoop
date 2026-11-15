---
id: BUG-1053
title: Tribe and Civ: the host runs a guest's command on member, vehicle and target pointers of the last table cycle without the frame's object list
status: fixed
kind: bug
stage: Tribe,civ
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-6
role: host
versions: 
fixed_in: 0.7.14
branch: fix/w49-live
commits: ba498632, b46e8265
tests: tribe_cmd_listed, civ_cmd_listed, play_tribe, play_civ
signature: a member / vehicle order whose target is a destroyed object of the host's last table cycle
docs: docs/re/tribe.md#tribe-cmd-listed, docs/re/civ.md#civ-cmd-listed, docs/06-testing.md#testing-same-pump-orders
related: BUG-0951, BUG-0919, BUG-0426
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-6. Not seen at runtime, found by code reading.
The host resolves a guest's Tribe or Civ command through its own tables, which it rebuilds from the game's lists every
0.12 s (Tribe g_hm) or 0.1 s (Civ g_hv). A member, vehicle or target the host's game removed since the last rebuild is
still in the table: the host then calls the game on it (DoAction, SetOrder / SetMoveOrder). BUG-0951 (group 3) judged
this window safe because a removed object is freed 10-40 frames after it leaves the list; this ticket keeps the
remaining risk (a destroyed but not yet freed object given an order; a low frame rate; a held command drained in the
frame of a world settle) and the reads of the same tables in hooks that run inside the game's update.

## Root cause
Observed in code (main efd2e0ba), confidence high for the code paths, the crash risk inferred (low):
* Tribe host_command (tribe.cpp:2877): the target of CMD_ACTION from resolve_target (tribe.cpp:2619, g_hm_by_id, no
  liveness check) goes into `a_DoAction(c, action, target)` (tribe.cpp:2954); the member `c` is checked with
  `game::object_alive(c)` only (tribe.cpp:2944), not game::listed / its kept serial (BUG-0951's tribe.kept_refs keeps
  the serial in g_hm but host_command does not compare it). give_on_host (tribe.cpp:2633) reads the member's
  +kOwnerTribeOff without a check.
* Tribe near_own_chief (tribe.cpp:4181) reads g_chief positions from h_CitizenTD, inside the game's update, before
  chiefs_tick rebuilds g_chief (reads only).
* Civ host_command (civ.cpp:3386): own_vehicle reads g_hv_by_id and the dead flag of the cached vehicle; resolve_target
  (civ.cpp:3373) returns g_hv_by_id / g_host_cities entries unchecked; then o_SetOrder / o_SetMoveOrder on them.
  host_command runs from on_message (civ.cpp:4870) and from the held-command drain in tick (civ.cpp:4572) before
  host_tick rebuilds g_hv.
* Civ reads of the same table: h_Capture (civ.cpp:1870, veh_pos of every g_hv entry), h_Convert (civ.cpp:2062,
  kVPurpose and veh_pos), for_each_owned (civ.cpp:5020), for_each_target host side (civ.cpp:5112, object_alive on the
  cached vehicle; the audit's civ.cpp:4594-4608 is an older line), own_selection (civ.cpp:6221, kVSelected of host and
  guest entries without gv_ok). These are reads of the old block, no game call.
Not a bug in the audit's list: CMD_GIVE's vehicle branch only changes our table (`g_hv[v].owner`).

## Fix idea
One shared check (the live pointer package): resolve a table entry through game::LiveRef / game::listed (inside the
command's game::LiveScope, one list walk) before any game call, compare the kept serial, and reject the command as
"gone" otherwise. The hooks inside the game's update (h_CitizenTD, h_Capture, h_Convert) ask game::listed without the
scope (it walks the list there). Switch idea: `coop.fix tribe.cmd_listed off` / `coop.fix civ.cmd_listed off`.

## How to test
Twin test with the reuse-hunt helpers (docs/06-testing.md#testing-freed-block-reuse): `coop.tribe reusehunt` /
`coop.civ reusehunt` make a removed member or vehicle's block come back as a new object; then a guest command for the
old id in the same pump as the removal (the BUG-0919 "same pump" method: `coop.tribe drop_member` + the command on two
console connections). Assert on the host's game that the new object at the reused address got no order (its AI order
/ target fields unchanged) and the command is rejected. Main: the order lands on the new object: FAIL.

## Fix
Tribe (`coop.fix tribe.cmd_listed`): host_command takes a member and a member target (resolve_target kind 1) of the
table g_hm only while it is the object the table entry was made for in the game's list (`hm_live`: game::alive with
the entry's serial, citizen noun; inside the frame's game::LiveScope for messages, a list walk for console / held
commands); otherwise the order is rejected ("the target is gone"). give_on_host checks the member the same way before
reading its tribe; near_own_chief (h_CitizenTD, inside the game's update) asks game::listed for the chief before
reading its position. Civ (`coop.fix civ.cmd_listed`): own_vehicle and resolve_target (kind 1 vehicle, kind 2 city)
use `hv_live` (game::alive, vehicle noun 0x18C6DE8, the entry's serial) and `host_city` (game::listed) for every
command kind (order, move, give, buy, build, buy city, capture); h_Capture and h_Convert skip entries that are gone
(one walk in a local LiveScope: no game code runs in their loops); for_each_owned, for_each_target and own_selection
collect only live entries in one walk and call their callbacks after it. Test commands: `coop.tribe drop_member`,
`coop.tribe guestcmd`, `coop.civ guestcmd`; `cmd_gone=` in both host status lines.

## Verification
Tests tribe_cmd_listed (tools/tests/coop_tribe_cmd_listed.py, tribe fixture) and civ_cmd_listed
(coop_civ_cmd_listed.py, civ_npc): the target dropped / removed and the guest's attack order on it in the same pump;
the ordered member's / vehicle's own order record in the host's game must not name the destroyed block.
FAIL (SPORE_FIX_OFF=tribe.cmd_listed): out/lab/20261113-022421 (3 of 3 rounds: `rec=7 target=<the dropped citizen>`).
FAIL (SPORE_FIX_OFF=civ.cmd_listed): out/lab/20261113-022453 (3 of 3 rounds: the order's target is the removed vehicle).
PASS: out/lab/20261113-022545, out/lab/20261113-023224-2, out/lab/20261113-024715 (tribe); out/lab/20261113-022656, out/lab/20261113-023224, out/lab/20261113-024715-2 (civ, the last runs after the status keys were renamed for test_lint).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-live (package L2): table entries through game::alive / listed (tribe.cmd_listed, civ.cmd_listed); FAIL/PASS twins tribe_cmd_listed, civ_cmd_listed.
- 2026-11-14: released in 0.7.14.
