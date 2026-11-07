---
id: BUG-0682
title: guest: caught fault in civ planner_city (game::cast on a stale city planner, coop.dll+0x1071C8) in the Space leaving frame
status: open
kind: crash
stage: Space
reported: 2026-11-07
source: lab 20261107-141346 campaign_space (rel/0.7.10 e7d1734c, lane 5)
role: guest
versions: 0.7.9+
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at coop.dll+0x1071C8 (game::cast, read of the vtable) data 0xC3BCCD6F, stack[0] = coop.dll+0x51F32 (civ::planner_city, civ.cpp:140)
docs: docs/re/civ.md#civ-planner-lifetime
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
  runs every frame in Tribe, Civ and Space (docs/re/civ.md#civ-planner-lifetime). planner_city() trusts it for 3
  frames and checks only that the editor's memory is readable, not that +0x40 still points to a live object.
* Inferred: in the leaving frame the city input strategy and its editor were freed (the stage teardown after the
  quit); the freed block is still readable, +0x40 holds garbage, and the next caller of planner_city()
  (planner_holds / planner_moves from a guest message or wsync's city row) casts it.
* Not a regression of 0.7.10: planner_city() and all its callers are unchanged since v0.7.9 (commit 6db6dcbd,
  BUG-0493, 0.7.4 era; `git diff v0.7.9..HEAD` adds no caller). First time seen in the lab logs.

## Fix
<!-- planned: forget g_editor on every stage / world change and at the quit (game::leaving), and check that the
     editor's +0x40 is readable with a vtable in the exe before the cast -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
