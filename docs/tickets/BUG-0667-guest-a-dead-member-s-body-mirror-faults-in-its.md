---
id: BUG-0667
title: guest: a dead member's body mirror faults in its eat node's exit when destroyed (0xAC9C7D data 0x39C under cTribe slot release 0xC988E0), then the game froze
status: open
kind: crash
stage: Tribe
reported: 2026-11-07
source: lab long_tribe on rel/0.7.9 (run 20261107-021641, lane 6)
role: guest
versions: rel/0.7.9 75599275
fixed_in: unreleased
branch: fix/w46-tribe-fault
commits:
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
* `kit.fix frameunwind` (sporekit/d3d9hook.cpp, the "never freeze" part): the frame keeps its stack address and thread;
  an entry on that thread at or above it (a nested call is always deeper) means the frame was cut off by a caught fault:
  the flags are cleared, a warning is logged, frames go on. Off: the flags stay (SporeKit 0.3.11).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
