---
id: BUG-0672
title: guest: a fault in cPlanetRecord_GetPlanetID+0x17 (0xCE6AE7, data 0x554) under the tribe slot release 0xC98A71 right after a Tribe world load, then the game froze
status: fixed
kind: crash
stage: tribe
reported: 2026-11-07
source: lab rel/0.7.10 20261107-073512 tribe_play (lane 9)
role: guest
versions: rel/0.7.10 94066205
fixed_in: unreleased
branch: fix/w46-tribe-load
commits: d612408c 76281c96 06d0b643
tests: coop_tribe_world_map.py (suite tribe_world_map)
signature: 0xC0000005 at 0x00CE6AE7 (labelled cPlanetRecord_GetPlanetID+0x17) data 0x554, eax 0, esi = a tribe of the world before the load, edi = tool type; calls 00CE6BCA 00C98A71 (Species_FootprintScale < cTribe_LayoutToolSpots 0xC98940 < cTribeTool_SetPosition 0xC9DB80 < coop make_tool)
docs: docs/re/tribe.md#tribe-stale-map-tool
related: BUG-0667 BUG-0523
---

## Report
A guest faulted 0.5 s after its Loading -> Tribe on a warm reset of tribe_play (lab 20261107-073512, lane 9), and the mod
stopped running frames for good (watchdog 30 s later). Rare: the rerun passed 4/4.

## Root cause
Details: docs/re/tribe.md#tribe-stale-map-tool.
* Observed (disassembly): 0xCE6AE7 is in 0xCE6AE0 under Species_FootprintScale 0xCE6BC0, which reads the species size
  (+0x554); the caller 0xC98A71 is in cTribe_LayoutToolSpots 0xC98940 (not cTribe_ReleaseSlot: "+0x191" was the nearest
  symbol), which passes the tribe's species (+0x18CC) unchecked. Type 6 comes only from a tool's SetPosition
  (cTribeTool_SetPosition 0xC9DB80) / 0xC9D700. So the null object is the species of the tool's tribe.
* Observed (code): the guest's wobj tool kind makes the host's tool in tribe_of_index(i), a mapping only each tribe
  state rebuilt; on_world_change did not clear it. Between a load and the new world's first tribe state it named a tribe
  of the world before (freed, readable, species released). The first session of that run had made tool 6 in tribe 1.
* Observed (runtime, forced, lab 20261107-094944): the mapping held over a reload, the tool made from the frame's tick:
  the same fault (esi = the old tribe, edi = 6, calls 00CE6BCA 00C98A71) and the same freeze.
* Old, not new in 0.7.10: since BUG-0523 (wobj.tribe_tools_all, v0.7.6). Not the BUG-0667 detour (tribe.body_teardown
  detours 0xC988E0, which is not on this path) and not Citizen_BTTerminate. Rare because the tribe state normally comes
  before the first tool snapshot after a load.
* Why kit.fix frameunwind did not help (observed, disassembly + watchdog stack): the fault was inside SporeKit's frame,
  but a task for outside the frame was waiting; pump_outside_tasks saw the frame flag still set and reposted
  WM_SK_PUMP_OUT at once, so App::Canvas::Tick's PeekMessage loop never ran dry and never returned to h_CanvasTick, where
  frameunwind clears the flag. Livelock in the message pump.

* The stray third player (another agent's lane-5 guest on lane 9's host port, 2026-11-07 ~07:55): in run
  20261107-073512 it is in mem_reserve.host.game.log only: player 2 'Bob 2' joined the lane-9 host at 07:55:19 (Creature,
  world 68AC222A), got a share save and left at 07:55:49. tribe_play's host log (08:09 on) shows only Bob, players=2, and
  the fault is on the guest, which the stray never reached; the cause (the stale mapping) reproduces with two players
  only (20261107-094944). It does not matter for this ticket.

## Fix
* `coop.fix tribe.world_map` (mods/coop/tribe.cpp): on_world_change clears the mapping; tribe_of_index returns an
  entry only while it is one of this world's tribes (else a warning, 8 times, and null).
* `kit.fix outpump` (sporekit/console.cpp, d3d9hook.cpp): inside a frame an outside task waits (no repost); the
  frame's end, Present's end or frameunwind's cut-off posts WM_SK_PUMP_OUT again. Any caught fault in a frame with an
  outside task pending froze the mod before; now frames go on.
* Test commands (guest): `coop.tribe statehold 1|0` (ignore the host's tribe states, prints the mapping),
  `coop.tribe frametool <index> <type> [m]` (make_tool from the next frame's tick, an outside task queued first).

## Verification
Test coop_tribe_world_map.py (suite tribe_world_map), lane 9, runs in the fix/w46-tribe-load worktree's out/lab:
* FAIL `SPORE_FIX_OFF=tribe.world_map,kit.outpump`: 20261107-094944 (the fault 0xCE6AE7 data 0x554, FROZE, no frames).
* FAIL `SPORE_FIX_OFF=tribe.world_map`: 20261107-095148 (the fault, repeated 1221 times by the game; "the frame 2283 was
  cut off ... frames go on": with kit.outpump the frames do not stop).
* PASS (all on) x3: 20261107-095340.
* Regressions (lane 9, all on): play_tribe, tribe_coop, tribe_body_teardown, tribe_body_menu, tribe_dead_member,
  long_tribe_short, detect_tribe 7/7 20261107-100128; tribe_play 5/5 20261107-103459; smoke 20261107-103836 12/14 (ui
  skipped on one lane; tribe_coop failed once on box-select / gather order, then passed 2/2 in 20261107-110255 and in
  20261107-100128: flaky, not this change); make test passes.
* Not verified: tribe_of_index's all_tribes guard on its own (the clear on a world change already empties the mapping in
  the test); the outpump path with a fault inside a Present-dispatched message (only the cut-off path was run).

## History
- 2026-11-07: opened.
- 2026-11-07: fixed on fix/w46-tribe-load (tribe.world_map, kit.outpump).

## Observed (2026-11-07, orchestrator)

- Run out/lab/20261107-073512, tribe_play (warm reset, lane 9), the guest at s 11.268, 0.5 s after Loading -> Tribe:
  `exception 0xC0000005 at 0x00CE6AE7 cPlanetRecord_GetPlanetID+0x17 (data 0x00000554)`, eax=0 edx=0,
  calls 00CE6BCA 00C98A71 00927EA0 00928CB9 00C9D0D2 00B20162 00B7F3BE 00C9D629 00C9D64C,
  i.e. under the tribe slot release 0xC988E0 (+0x191, the cTribe_ReleaseSlot that BUG-0667 detours for a null tribe).
- 30 s later the watchdog saw no frames (stage 'SporeApp::Init done'), main thread in the message pump: the game froze
  for the mod although 0.7.9's kit.fix frameunwind is in.
- Also failed in that run: "the same chiefs on both machines", "the guest is told about the epic", host's puppet gate
  (the guest's state older than 5 s), and a report-20261107-081124-i18.txt.
- Not seen before in any lab log of any worktree. Rerun 20261107-082717: tribe_play 4/4 PASS (rare).
- Questions: is it the 0.7.9 tribe.body_teardown detour or code new in 0.7.10 (P0-Cell, M1 merged)?
  Why did frameunwind not get frames going again (the fault may be outside SporeKit's frame)?
- 2026-11-07 (fix/w46-final-triage): suite.py allows the gate finding the test makes on purpose (lab 20261107-141346: tribe_world_map, sync-check `t: 7 missing on the guest` at s 32.0, 0.5 s after the test let the held tribe states in again).
