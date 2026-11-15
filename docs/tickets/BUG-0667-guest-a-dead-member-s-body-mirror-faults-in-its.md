---
id: BUG-0667
title: guest: a dead member's body mirror faults in its eat node's exit when destroyed (0xAC9C7D data 0x39C under cTribe slot release 0xC988E0), then the game froze
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-07
source: lab long_tribe on rel/0.7.9 (run 20261107-021641, lane 6)
role: guest
versions: rel/0.7.9 75599275
fixed_in: 0.7.9
branch: fix/w46-tribe-fault
commits: d893d8f6 39231fee ea7ece1a 991dad47
tests: coop_tribe_dead_member.py --eat (suite tribe_body_teardown)
signature: 0xC0000005 at 0x00AC9C7D (labelled Relevance_InsideEnter+0xF4D) data 0x39C, ecx 0x394, esi 0xD; calls 00C27E9C 00C988FA 00DB0C6E 00BC87D0 00BC887D 00C0D596 00C20013 (cTribe_ToolLayout < cTribe_ReleaseSlot < eat node exit 0xDB0C40 < BT shutdown 0xC0D500 < citizen teardown 0xC1FFE0)
docs: docs/re/tribe.md#tribe-body-teardown
related: BUG-0620 BUG-0260
---

## Report
long_tribe on rel/0.7.9 (lane 6, quiet lab) failed at s ~1013-1056: one caught fault on the guest, then "guest: FROZE
(no frame for 10 s)". The guest's log ended at the fault (no line after it, no watchdog dump before the lab ended):

    exception 0xC0000005 at 0x00AC9C7D SporeCoop::Relevance_InsideEnter+0xF4D (data 0x0000039C) ... ecx=00000394 esi=0000000D
    calls: 00C27E9C 00927987 00F47234 00C260F2 00C988FA 00DB0C6E 00BC87D0 00BC887D 00C0D596 00C20013

The same address and data as BUG-0260 (a debug command's object action on a citizen), another path. Before it: an editor
visit of the host at 14.9 min (the guest played its tribe locally meanwhile), `bodies_detached` 4 -> 5 at s ~1000 (a
member of the host's tribe died, tribe.member_body), a raid drama at s 1008. long_tribe passed twice on 0.7.8.

## Root cause
New in 0.7.9 (BUG-0620's detach, commit e485cc7a; v0.7.8 has no detach_body).
* Observed (disassembly, docs/re/tribe.md#tribe-body-teardown): ecx 0x394 = null + 0x394, the tribe of
  cTribe_ToolLayout(tribe+0x394). The real return addresses of the chain: 0xC988FA in cTribe_ReleaseSlot 0xC988E0
  (ret 8, it looks the layout up before it checks the slot), 0xDB0C6E in 0xDB0C40 = the exit of the eat node
  0x0461300F (tribe = citizen+0x1014 read by 0xC230B0, layout type 0xD = the food mat), 0xBC87D0 / 0xBC887D the tree's
  node runner, 0xC0D596 in 0xC0D500, 0xC20013 in the citizen teardown 0xC1FFE0 (0xC27CD8, 0xC1CA4F below it in the repro).
* Observed (code): on the guest, tribe.member_body's detach_body clears the body mirror's +0x1014 and remove_member
  destroys it with DestroyInstance when the host's body goes. The game's own RemoveMember destroys with the tribe set.
* Observed (lab 20261107-031255): a mirror whose tree ran while the host was away (local_play; the test uses
  `coop.tribe localbt` + `coop.tribe eat`) keeps the eat node active when its tree is skipped again; the host's member
  dies, its body goes, the guest faults exactly there. In long_tribe the guest's own play during the editor visit put a
  mirror in that node (inferred: no per-member log of it in that run).
* Why one caught fault looked like a freeze (observed, lab 20261107-031255, the watchdog's stack and code): the fault
  happened inside SporeKit's frame (d3d9hook.cpp on_frame_begin_present, `g_in_frame = true` -> SK_EV_FRAME -> coop).
  The game's own handler caught it and unwound past SporeKit's frames, so `g_in_frame` stayed set and every later
  Present / loop pass returned at once: no frame counter, no mod tick (the host saw the guest's state get 9.5, 19.5,
  29.5 s old), while the game's main thread went on in App::Canvas::Tick's message pump (watchdog stack 30 s later).

## Fix
* `coop.fix tribe.body_teardown` (mods/coop/tribe.cpp): detach_body keeps the tribe it took the mirror out of; right
  before the guest destroys a detached body the tribe goes back into +0x1014 when it is still one of the world's (as
  the game's RemoveMember destroys); at session end the bodies left in the world get it back too; a cTribe_ReleaseSlot
  (0xC988E0, new .sym entry) with this = null is skipped by a detour (warned 8 times). Off: destroyed with no tribe, no
  detour (rel/0.7.9 before).
  Added after long_tribe_short 20261107-032137 (a citizen with no tribe torn down at the menu exit, another node's
  exit 0xDA7FB0 faulted after the skipped release): on a guest SetTribe(null) is recorded and the citizen teardown
  (vt 0xC4, 0xC27CD0) with no tribe puts back the detached or recorded tribe (AddRef for the recorded one), else it
  empties the tree's stack so no exit runs. Same switch.
* `kit.fix frameunwind` (sporekit/d3d9hook.cpp, the "never freeze" part): the frame keeps its stack address and thread;
  an entry on that thread at or above it (a nested call is always deeper) means the frame was cut off by a caught fault:
  the flags are cleared, a warning is logged, frames go on. Off: the flags stay (SporeKit 0.3.11).

Follow-up (2026-11-07, after the merge 640be4eb): long_tribe_short failed on rel/0.7.9 (lab 20261107-043030, lane 9):
the fallback logged 3 citizens with no tribe at the menu exit and the exit 0xDA7FB0 still faulted, then the world load
from the galaxy menu faulted twice in space gfx and the guest stayed loading. Two causes:
* the fallback set the tree's depth (ai+0x1DC) to 0, which does not stop the exits: 0xBC8850 skips them only when the
  stack's first word ai+0x1D0 is -1. It now fills the 0x418-byte stack with 0xFF as the game leaves it.
* the source (diagnostics, lab 20261107-052358): those citizens are detached bodies still lying when the guest leaves
  to the menu. The world's teardown removes the tribes first, so their tribe can no longer be put back. Fix (same switch):
  detach_body ends the body's behaviour tree with Citizen_BTTerminate (0xC0D500) while +0x1014 is still set (the node
  exits release their spots as the host's dying member's did), and a detached body's BT tick stays skipped.

## Verification
`coop_tribe_dead_member.py --eat` (suite `tribe_body_teardown`, lane 4):
* FAIL with `SPORE_FIX_OFF=tribe.body_teardown` on SporeKit without frameunwind (d893d8f6): lab run 20261107-031255
  (the fault at 0xAC9C7D data 0x39C, same chain as long_tribe, then "guest: FROZE", watchdog stack in Canvas::Tick).
* FAIL with `SPORE_FIX_OFF=tribe.body_teardown,kit.frameunwind` (final code): lab run 20261107-042104 (fault, FROZE,
  the guest's frame count stood still 4000 -> 4000).
* FAIL with `SPORE_FIX_OFF=tribe.body_teardown` only: lab run 20261107-031958 (the fault, but "the frame 3867 was cut off
  by a fault ... frames go on": no freeze, the body went, frames ran: kit.frameunwind alone keeps the game going).
* PASS: lab runs 20261107-031815 and 20261107-035147 (no fault, teardown_restored=1, the body went after ~33 s).
* Regression (final code c88e9045, lab run 20261107-035147, 7/7): long_tribe_short, play_tribe, tribe_coop,
  tribe_dead_rejoin, tribe_body_teardown, tribe_dead_member, tribe_dead_player. long_tribe_short logged the teardown
  guard once ("citizen ... torn down with no tribe in node 04861814 (depth 2): its tree's exits skipped") at the menu
  exit, with no fault; the run before that guard (20261107-032137) faulted there (0xDA7FC9).
* Menu path, `coop_tribe_dead_member.py --eat --menu` (suite `tribe_body_menu`: the body lies out of its tribe when
  the guest leaves to the main menu): FAIL with `SPORE_FIX_OFF=tribe.body_teardown` lab run 20261107-053541 (0xAC9C7D,
  then 0xC70A20 and 0x10350D7 at the world load, as in long_tribe_short); PASS 20261107-053655 and in 20261107-053835.
* A/B: long_tribe_short on v0.7.8 passed (rel/0.7.8 worktree, lab 20261107-052252); new in 0.7.9.
* Gate (991dad47): lane 5 lab 20261107-053835 8/8 (play_tribe, tribe_coop, tribe_dead_rejoin, tribe_body_teardown,
  tribe_dead_member, tribe_dead_player, detect_tribe, tribe_body_menu); long_tribe_short x3 lane 4 lab 20261107-053815.
  long_tribe_short x3: 3/3 PASS, 0 faults on both games; the teardown fallback still logged 1-2 citizens in node
  0x04861814 with no tribe (not detached, no SetTribe(null) seen; inferred: bodies loaded without a tribe with the
  received world) at the later menu exit and ended their trees without exits (docs/re/tribe.md#tribe-body-teardown).
* (before) Negative: `--eat --menu` (the mirror in the eat node, then Exit to Main Menu) passed with the fix off (lab run
  20261107-035000): not a repro of the menu path, dropped.

## History
- 2026-11-07: opened (long_tribe on rel/0.7.9, lab 20261107-021641).
- 2026-11-07: fix/w46-tribe-fault: reproduced (tribe_body_teardown), new in 0.7.9 (BUG-0620's detach_body); fixed
  (tribe.body_teardown) and the freeze after a caught frame fault fixed in SporeKit (kit.frameunwind).
- 2026-11-07: long_tribe_short failed after the merge (rel/0.7.9 lab 20261107-043030): the fallback did not stop the exits and the bodies outlived their tribes at the menu exit; fixed at the source (detach ends the tree), test tribe_body_menu.
- 2026-11-07: released in 0.7.9.
