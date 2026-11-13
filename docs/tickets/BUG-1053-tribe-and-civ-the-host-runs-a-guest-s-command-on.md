---
id: BUG-1053
title: Tribe and Civ: the host runs a guest's command on member, vehicle and target pointers of the last table cycle without the frame's object list
status: open
kind: bug
stage: Tribe,civ
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-6
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
