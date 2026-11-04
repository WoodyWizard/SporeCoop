---
id: BUG-0505
title: play_tribe: guest orders the host takes but its members do not act on (12 idle of 28), the host's bot gives too few orders (minimap look_far 24)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-03
source: fix/w40-play final check, lab 20261103-160342
role: 
versions: 
fixed_in: 0.7.5
branch: fix/w41-tribe-orders
commits: 19fa95a7 8e0fac53 37254c57
tests: tribe_tools (coop_tribe_tools.py), play_tribe
signature:
docs: docs/re/tribe.md#tribe-orders, docs/06-testing.md#testing-tribe-orders
related: BUG-0481
switch: tribe.grab_spot
---

## Report
Seen once the citizens' sync check passed (BUG-0481): play_tribe#1 of out/lab/20261103-160342 (fix/w40-play,
lane 6) failed `host: gave orders through real clicks (8)` and `guest: the members act on the orders ({'ok':
12, 'lost': 0, 'idle': 12, 'none': 4})`. The guest's orders reached the host (`cmds_done=29 rejected=0`), but the
ordered members did not move or hit there ("no movement": gather fruittree, the epic n11 in the epic phase, a
walk after a gift). The host's bot: look_far=24 (its minimap look did not land within 30 m), no_selection=18,
84 actions in 10 min. Replay: `--seed 655950227`.

## Root cause
fix/w41-tribe-orders (GOG 2024, Ghidra + runtime on lane 9; docs/re/tribe.md#tribe-orders):
* Observed (disassembly): the game's own order (a click: DoActionForAllSelected -> 0xCCFE70 -> DoActionForObjects ->
  cCreatureCitizen::DoAction(action, target, prop), then the selected members' voice and a sound) and host_command's
  DoAction(c, action, target, 0) are the same call with the same queue handling (0xBCB0E0 clears the queue and adds
  one record), stimulus and re-plan; the UI sets no flag or priority. The ticket's premise "the record is replaced by
  the game's own behaviour" is not what happens: the members were IN the ordered behaviour.
* Observed (runtime, `coop.tribe ai`, tribe_tools lab 20261104-014156 #2): the guest's member in the GrabTool node
  0x0462854D, node state 1 (waiting for the walk to the claimed grab spot), spot 0 of the rack's 6 (a ring ~6.8 m
  round the rack, `coop.tribe layout 1`), walk goal (406.1 -7.1 302.7) = that spot, 2.2 m from the fire pit's
  centre; the member (radius 2.87) stood 1.5 m short of it for 90 s. State 1 has no time limit: it stays forever.
  The re-order at 40 s made an identical record, and DoAction posts the re-plan stimulus only for a changed record.
  This is the game's own behaviour, the same for any order of that member (a host click too): the rack built 9 m
  off the centre has a spot the member cannot stand on.
* The play_tribe idles of the earlier runs (hunt: node 0x05F79546, a reaction to an area blow that plays
  anim 0x5F73E48, entered on stimulus (0, 0x4000000) from 0xC20390; gather: node 0x0569236E, the gather behaviour)
  were not reproduced in 2 runs here (lab 20261104-014832: guest 11 ok / 0 idle, 14 ok / 1 idle; host 0 idle):
  inferred, members hurt and knocked by the epic's blows, and gathers on trees out of the behaviour's range
  (its table entry has 100.0 next to the node id), the same for host orders (the host's own orders had idle
  ones too: ok 9 of 14 in out/lab/20261104-010027).
* Earlier inference (superseded): the idle orders cluster in the second half (gather / epic) after member gifts
(`gave=1/2`): the ordered member may be the one just given; the host's look_far suggests its view calibration
went off after the epic arrived.

## Fix
`tribe.grab_spot` (host, mods/coop/tribe.cpp grab_spot_tick): a player tribe member in the grab node's state 1
that stands still (moves < 0.3 m, speed < 0.3) for 4 s within 3 m of its walk's goal is set onto the goal facing
the tool; the game's behaviour then goes on (arrival, the grab animation, SetSpecializedTool). For every member
(the host's own too: the bug is the game's, not the guest path). `coop.fix tribe.grab_spot off`: it stands there.
host_command is unchanged (it already is the game's own order). Test: tribe_tools over --repeat (the stop is not
deterministic; `coop.tribe put` of the member or of a blocker next to the spot by the fire pit did not make it).

## Verification
* FAIL without the fix (code before 8e0fac53, the diagnostics only): out/lab/20261104-014156 tribe_tools#2 (the
  member 1.5 m short of its grab spot for 90 s, no weapon). Twin `SPORE_FIX_OFF=tribe.grab_spot` over 6 runs:
  out/lab/20261104-021539 6/6 OK within the 90 s, #4 stood 0.85 m short ~30 s after the re-order (135 s): the
  stop is not deterministic, so the twin does not fail every time.
* PASS with it: out/lab/20261104-022327 tribe_tools --repeat 8 --cold, SPORE_DETECTOR=20: 8/8; in #3 the fix set
  the member onto its spot ("stood 2.5 m short of its spot at a tool for 4 s: set onto it") and it took the weapon.
  Before: 3 of 8 runs failed (T1, out/lab/20261104-012256, 20261104-005643).
* play_tribe (lab 20261104-014832, 2 runs, the diagnostics build): `members act on the orders` passed both
  (guest 11/0 idle, 14/1 idle); #1 failed the lab gate on an NPC proxy 3 m off (BUG-0503 area, not this ticket).

## History
- 2026-11-03: opened.
- 2026-11-03 (fix/w41-tribe-actions): replayed with `PLAY_TRIBE_SEED=655950227` (new: the lab passes the seed this way) on lane 2, SPORE_DETECTOR=20: not reproduced, PASS (out/lab/20261104-000550: host 19 orders all acted on, guest 15 ok / 2 idle / 0 lost of 17; the seed fixes the bots' choices, not the game). play_tribe now logs, for an order nobody acted on, the host's line of each ordered member (behaviour node `bt=`, place, health) to tell the next failure apart. Suspect (inferred, not shown): gathers on trees the host's game has picked empty while the guest's copy still shows fruit (worldobj tree snapshots cover at most 48 trees within 70 m of the village; the bot picks trees within 90 m).
- 2026-11-04 (fix/w41-tribe-actions): reproduced in the regression, play_tribe#2 of out/lab/20261104-010027 (guest ok 11 / idle 8 / lost 0; host gave 14 orders: 'gave orders' passed, ok 9). Observed with the new host lines (the host's game state 8 s after the order): epic order (action 10 Hunt, target the co-op epic at (373 -168 295)) on members #12 hp 23, #13 hp 8.5, #18 hp 112, all standing (speed 0) ~85 m from the epic at (320 -157 360), all three in the same behaviour node bt=05F79546; gather order (action 2, a tree at (425 -79 265)) on #20 at the village (405 18 303), 100 m off, standing in node bt=0569236E. So the host's game took the orders (no rejection) and its members run another behaviour: inferred, the order's AI record is replaced at once by the game's own (hurt members: a heal / flee node; the gather: the tree out of the member's reach or the order dropped for distance). Next: name the nodes 05F79546 / 0569236E (behaviour ids, docs/re/tribe.md section 4) and log the member's action queue right after host_command's DoAction.
- 2026-11-04 (fix/w41-tribe-actions): the same symptom in tribe_tools (BUG-0514's test) in 3 of 8 runs with the fix on: the guest's GrabTool order reaches the host (`-> <rack>`, cmds_done+1) and the host's member stands 6 m from the rack (speed 0, bt=0462854D) and never takes the tool, also after a second order at 40 s (out/lab/20261104-012256 tribe_tools#2, out/lab/20261104-005643); 5 runs took it in 3-37 s. So a guest's order done on the host by host_command's DoAction(c, action, target, 0) is sometimes not carried out by the host's game: this ticket's root cause, not the tool sync.
- 2026-11-04 (fix/w41-tribe-orders): the order path is the game's own (DoAction, docs/re/tribe.md#tribe-orders); the
  member stands in the GrabTool node waiting for a walk to a grab spot that stopped short. Seen twice with
  `coop.tribe ai`: out/lab/20261104-014156 tribe_tools#2 (1.5 m short of the spot by the fire pit, 90 s, FAIL),
  out/lab/20261104-021539 tribe_tools#4 with
  `SPORE_FIX_OFF=tribe.grab_spot` (0.85 m short of the spot on the far side, no fire pit near; it took the tool
  ~35 s after the re-order: 135 s run). The twin over 6 runs (20261104-021539) passed 6/6 within the 90 s: the stop
  is not deterministic. Also seen: the first order ends within 9 s without the tool in some runs (record gone,
  node 0x051C71AF, table entry 0x1593AC0, enter 0xDAFAF0 / tick 0xDAB180, also a slot user); the re-order at 40 s
  then runs the grab. ? why it ends. The one `--own` control run picked the host's chief (tool 11, no grab: the
  test now skips chiefs); a control with a host's ordinary member was not run.
- 2026-11-04 (fix/w41-tribe-orders, regression out/lab/20261104-030728 play_tribe#3, guest idle 4 of 17; the
  run failed on the host bot's `gave orders through real clicks (7)`, the title's second half): **observed** the
  play_tribe idle case: member #29 has the guest's new order record (rec=4 walk, then rec=10 hunt the epic, target
  set) but its behaviour node is still the gather node 0x0569236E, state 1, its walk goal the old tree (424.6 -78.8
  264.9) 100 m off, walk mode 1, not moving; #36 the same node, state 0. So the new record does not take the member
  out of the gather node: the node's state 1 keeps returning "running" (tick 0xD75F70 case 1: waits while
  `c+0x38 list empty or +0xFC8 != +0xFC4` and 0xC26DF0(0x30000) is false) and the re-plan stimulus 0x4000 of DoAction
  does not end it. Inferred: the same for the host's own orders on a member stuck in a gather (the host had 2 idle
  orders in #2). Not fixed: the grab half is (tribe.grab_spot); status stays open for the gather half.
- 2026-11-04: the grab half (a member that stops short of its grab spot) fixed by tribe.grab_spot on fix/w41-tribe-orders (tribe_tools 5/8 -> 8/8); the gather-node stall split into BUG-0531.
- 2026-11-04: released in 0.7.5.
