---
id: BUG-0531
title: Tribe: a member stalled in the gather node (0x0569236E, state 1, walk goal an old tree 100 m off) does not act on a new order
status: open
kind: bug
stage: Tribe
reported: 2026-11-04
source: split from BUG-0505 (fix/w41-tribe-orders, 2026-11-04)
role: guest
versions: 
fixed_in:
branch: fix/w42-tribe-left
commits:
tests: tribe_gather_stall (coop_tribe_gather_stall.py), tribe_gather_block (coop_tribe_gather_block.py)
signature:
docs: docs/re/tribe.md#tribe-gather-orders
related:
---

## Report
Seen in play_tribe (out/lab/20261104-030728 #3 in the fix/w41-tribe-orders worktree): member #29 has the guest's new order record (walk, then hunt the epic, target set) but stays in the gather node 0x0569236E, state 1; its walk goal is still the old tree 100 m off and it does not move. A new order does not take a member out of a stalled gather. Inferred, not shown: host orders suffer the same. Details: docs/re/tribe.md#tribe-orders. Target: 0.7.6 or a 0.7.5.x.

## Root cause
fix/w42-tribe-left (GOG 2024, Ghidra + runtime lane 6, docs/re/tribe.md#tribe-gather-orders):
* Observed (disassembly): the gather node's state 1 (tick 0xD75F70) returns "running" until the member carries a
  bundle (its own cGameBundleContainer c+0x38) or holds a 0x30000 handheld, and then only leaves on the walk's
  arrival (0xC42F80): no time limit. The exit 0xD75520 does not touch the order queue.
* Observed (runtime, 7 runs of the new tribe_gather_stall test): a guest's walk order given while its member is in
  the gather node (state 1 walking to a tree 90 m off, or state 19 carrying the food home) took the member out of
  the node every time (node 0x04626360 within 3 s, at the goal on both games).
* Inferred, not shown: the play_tribe stall needs state 1 with the walk itself stopped short of the tree (standing,
  walk mode 1), the same kind of stop as the grab spots of BUG-0505. Not reproduced, so not fixed.

* fix/w45-last (L5 of 0.7.9), a serious attempt with new diagnostics (`coop.tribe ai` ends with the order target's
  object; play_tribe prints every order nobody moved on with the host's ai lines), observed:
  - play_tribe x2 with the detector (lab 20261106-132448, 20261106-133817): 3 + 2 orders without movement, none in the
    gather node 0x0569236E. Four were members in the BT node **0x05218BB3 = the creature fight node** (node table .data
    0x1583F40: enter 0xD69540, tick 0xD69740, exit 0xD673B0): the leader (#1, tool 11) given a gather order, a member
    carrying 10 food given a social order to a tribe 160 m off, three members raiding a hut 7 m from it; the game keeps a
    member in a fight, as for the host's own clicks. One was a member already carrying 9.5 food (rec=2, node
    0x04600D16). So play_tribe's "idle" orders on the current code are the game's fights, not a stall.
  - tribe_gather_block (new probe, lab 20261106-141225): a guest's member put 6 m behind a hut and sent to gather a fruit
    tree 21 m beyond it: the gather walk went round the hut (state 1, walk mode 1, goal r=6.7 m), reached the tree in
    3.4 s (state 2, walk mode 3, r=0.1), picked (4, 6) and carried home (19): no stall.
  - The order path: a guest's gather order on a tree the host's flora does not have within 1 m is rejected on the host
    (`rejected: no such target here`, play_tribe counts it `lost`), so a "tree gone or replaced" order never reaches a
    member; a gather order 90-120 m off walks (tribe_gather_stall, 7 runs of fix/w42-tribe-left, T2's teleports).
  Not seen in 0.7.9 code: the 0569236E state 1 stall with the walk stopped short. Inferred: the 2026-11-05 stall
  (walk mode 1, goal 2.8 m from a standing member) was a walk blocked by the ground or an obstacle the game's own
  pathing did not get round; no mod object takes part in it (the host's member runs the game's own AI).

## Fix
None (not reproduced). A watchdog that ended the gather node (state 0xF) / re-gave a lost order was written and
removed again: without a failing run it could not be shown to help. Diagnostics added: `coop.tribe ai` prints the
food carried (`carry=`) and the held tool; `coop.tribe order <id> <action> <x y z>` (host) gives a member an order
as host_command does.

## Verification
no-fail-run: not reproduced in 0.7.9 (play_tribe out/lab/20261106-132448, 20261106-133817: the idle orders were the fight
node 0x05218BB3; tribe_gather_block out/lab/20261106-141225: no stall behind a hut). Closed not-reproduced; reopen with a
play_tribe log whose `orders without movement` block shows node 0569236E state 1 (the ai line has the walk, goal r and
the target).
no-fail-run: not reproducible with tribe_gather_stall (coop_tribe_gather_stall.py), which passed its order checks in
out/lab/20261104-100046 (2/2), 20261104-100604 (3 runs: 2 OK, #3 failed only its control walk, the walk ending at
the hut's edge), 20261104-100245 (order checks: 1 of 2 runs had the walk stop 8.8 m short at an obstacle). The test
stays as a regression check (suite tribe_gather_stall).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-tribe-left): gather node RE, not reproduced in 7 runs, no fix; regression test tribe_gather_stall.
- 2026-11-05 (fix/w44-tests, G2): seen again in play_tribe, fix/w44-tribe-guest out/lab/20261105-115848 (guest ok 13 /
  idle 11, the BUG-0505 class line of the G2 task; the idles came after s ~280, 2 -> 4 -> 8 -> 11). Observed (the host
  lines of the last 4 idle orders, the only ones the log keeps): two members stood still on the host (speed 0): #2 in the gather node 0x0569236E with
  the new order's record (rec=2, target set), walk mode 1, its walk goal (391.9 -93.1 314.6) 2.8 m from it, the tree
  (401.6 -64.9 303.9) 34 m off, ordered to that tree twice; #9 in node 0x05F79546 (rec=4 walk, then rec=2 gather)
  ordered twice. So the stall this ticket infers (the gather node with the walk stopped short, a new order not taking
  the member out) happened; inferred: most of the 11 idles are repeated orders of the same stuck members. The check "the members act on the
  orders" measured the game here, not the bot: left as it is (G2 changed only the bots' order pace, BUG-0397).
  Not reproduced on purpose; still open.
- 2026-11-05 (fix/w45-tribe, T2 of 0.7.9): time-boxed, no code change kept. The class fix (the walk-stall watch of
  tribe.grab_spot, BUG-0505, extended to the gather node's state 1 behind `tribe.gather_stall`) was written and taken
  out again: no run failed without it. The stall was made on purpose in a new test (a guest's member sent to gather
  a fruit tree 60 m off, put by a teleport short of its walk's goal while it walked in state 1): with the watch off
  the game's walk went on both times: put 2.5 m short (inside the tree goal's arrival radius r=6.2 m) it arrived at
  once (states 5, 2: lab 20261105-232113); put 7.6 m short (outside r=5.1 m) it walked on and arrived 10 s later
  (lab 20261105-232555). Observed: a tree goal of the gather has an arrival radius of 5-6 m (`coop.tribe ai`: r=), and
  the gather's tree field +8 read 0 in state 1 (the tree is in the order record). Inferred: the play_tribe member
  standing 2.8 m from its goal in state 1 had a goal that was not reached by the radius check (its r under 2.8 m:
  not a tree goal, ~ an intermediate goal of an obstacle walk), so a watch keyed on the distance alone would not have
  matched that state either. Next: catch it in play_tribe with `coop.tribe ai` of the stuck member (its r and walk
  flags) before choosing a fix. Stays open.
- 2026-11-06 (fix/w45-last, L5 of 0.7.9): two play_tribe runs with the new diagnostics and a hut-obstacle probe: no stall; the idle orders are the game's fight node; closed not-reproduced.
- 2026-11-08 (fix/w46-g4-tests): G4 (2026-11-08) judging the play_tribe 'orders' flakes: seen again in fix/w46-k4-bodies out/lab/20261108-020506-2 play_tribe ('guest: the members act on the orders {ok 14, lost 0, idle 8}'): three of the guest's four members stood (speed 0, walk mode 1) with new order records (rec=9/20/10/2): #2 in the gather node 0x0569236E state 1, carry=4.0, walk goal an old tree 58 m off (this ticket's signature); #1 and #9 in node 0x0462854E then the food delivery node 0x04623E0C (goal r=10): the same class in another node. Only this run of 62 play_tribe runs of 2026-11-06..08 has it. Reopened: needs the dedicated fix (on the host, a member standing in a node foreign to a new order for 3 s gets the walk action first, the class fix tried in fix/w42), not part of G4's test work. The run's other failure (a fruit rejected) is BUG-0744.
