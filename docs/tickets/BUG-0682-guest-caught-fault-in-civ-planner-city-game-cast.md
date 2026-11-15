---
id: BUG-0682
title: guest: caught fault in civ planner_city (game::cast on a stale city planner, coop.dll+0x1071C8) in the Space leaving frame
status: fixed
kind: crash
stage: Space
reported: 2026-11-07
source: lab 20261107-141346 campaign_space (rel/0.7.10 e7d1734c, lane 5)
role: guest
versions: 0.7.9+
fixed_in: 0.7.11
branch: fix/w46-k7-findings
commits: ac7d4c7c
tests: planner_leave
signature: 0xC0000005 at coop.dll+0x1071C8 (game::cast, read of the vtable) data 0xC3BCCD6F, stack[0] = coop.dll+0x51F32 (civ::planner_city, civ.cpp:140)
docs: docs/re/civ.md#civ-planner-lifetime, docs/re/civ.md#civ-planner-stale-editor
related: BUG-0493, BUG-0667
---

## Report
campaign_space on rel/0.7.10 (lab 20261107-141346, lane 5) failed on `[fault] Carol: 1 new caught fault(s)`. The guest
extra1 (Carol, 4 players) left a Space planet for the galaxy menu (`leaving frame 1.003 s after the quit`); 0.3 s later:

    exception 0xC0000005 at 0x6E1571C8 coop.i25.1.dll+0x1071C8 (data 0xC3BCCD6F) ... ecx=C3BCCD6F ebp=00000000
    frames: the frame 33832 was cut off by a fault the game's handler caught (1 so far); frames go on (BUG-0667)

(campaign_space.extra1.game.log line ~757; findings out/lane5/campaign/20261107-142609/findings.md). The game went
on (kit.fix frameunwind), the bot timed out a bit later.

## Root cause
* Observed (llvm-symbolizer on build/coop.dll of e7d1734c): +0x1071C8 is `game::cast` (game.cpp:269, `mov eax,[ecx]`
  on the object), the object 0xC3BCCD6F is garbage. The first stack word 0x6E0A1F32 = coop.dll+0x51F32 is the return
  into `civ::planner_city()` (civ.cpp:140): `game::cast(F<void*>(g_editor, kEdCity), kCityType)`.
* Observed (code): `g_editor` is the cCommunityEditor whose update (0xD124E0, hooked by h_EditorUpdate) ran last; it
  runs every frame in Tribe, Civ and Space (docs/re/civ.md#civ-planner-lifetime). planner_city() trusted it for 3
  frames and checked only that the editor's memory is readable, not that +0x40 still points to a live object.
* Observed (lab 20261108-003304, coop_planner_leave, fix off): the update runs on every Space frame; in each quit to the
  galaxy menu planner_city() read the freed editor on 3 frames of the leaving stage and 2 of them faulted in game::cast
  (8 caught faults in 4 quits, data 0x4394C5EB, the cCity type 0xEE9B2232 on the stack): the campaign_space fault,
  made on purpose by a probe that calls planner_city() every frame as wsync's city rows and the guest's Civ messages do
  now and then.
* Not a regression of 0.7.10: planner_city() and all its callers are unchanged since v0.7.9 (commit 6db6dcbd,
  BUG-0493, 0.7.4 era; `git diff v0.7.9..HEAD` adds no caller).

## Fix
`civ.planner_stale` (civ.cpp planner_city): the editor of the last update is trusted only in the mode of that update
and not while the stage is leaving (game::leaving_stage, set by the quit and the load); it is forgotten then (the next
update sets it again while the planner lives). Its +0x40 must be readable and have a vtable in the game's image before
the cast. Test probe: `coop.civ planner_probe on|off` (planner_city() once a frame in every mode, counts the frames in
which it read an editor of another mode or of a leaving stage). `coop.fix civ.planner_stale off`: 0.7.10.

## Verification
Lab, lane 10, `planner_leave` (coop_planner_leave.py: space fixture, the guest's probe on, 4 quits to the galaxy menu
and back):
- FAIL (SPORE_FIX_OFF=civ.planner_stale): lab 20261108-003304, 2 of 2: stale reads 12 / 11 frames, guest faults 8
  (0xC0000005 at coop.dll+0x109D88 = game::cast of that build, data 0x4394C5EB) in each.
- PASS: lab 20261108-003641, 2 of 2: stale reads 0, the editor refused 10 times, no fault on either game.

## History
- 2026-11-07: opened.
- 2026-11-08 (fix/w46-k7-findings): reproduced (planner_leave, 8 caught faults per run with the fix off) and fixed (civ.planner_stale).
- 2026-11-08 (fix/w46-k7-findings): campaign_space on the final build, lane 10, lab 20261108-012301: OK, 0 caught faults on all four games.
- 2026-11-08: released in 0.7.11.
