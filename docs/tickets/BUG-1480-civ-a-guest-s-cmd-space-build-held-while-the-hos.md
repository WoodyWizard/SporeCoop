---
id: BUG-1480
title: Civ: a guest's CMD_SPACE_BUILD held while the host is in an editor or loading is never applied (a paid colony building missing) (CIV-11)
status: fixed
kind: desync
stage: Civ, Space
reported: 2026-11-23
source: audit
role: host
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w78-civ
commits: 9b9fdf4a, 9479fa44
tests: space_planner_menu, space_planner_menu_off (tools/tests/coop_space_planner_build.py --menu); unit tests/civ_tests.cpp test_audit_v1
signature:
docs: docs/re/civ.md#civ-audit-v1-0717
related: BUG-0751, BUG-0766, BUG-0693
switch: civ.space_build_route
---

## Report
External static audit of 0.7.15.5, item CIV-11 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-V1 (fix/w78-civ). A behaviour change gets a switch and a twin.

## Root cause
* Code (rel/0.7.17, civ.cpp `on_message`): a guest's CMD_SPACE_BUILD went to `host_space_build` only when
  `game::mode_id() == modes::kSpace`. A host in an editor (the captain's outfitter from Space) or in a load fell into
  the Civ command hold `g_held.hold` (BUG-0693), which only `civ::tick` drains, and only in Civ: the building the
  guest paid for never came to the host's colony; `host_space_build`'s own "not in Space -> space_hold" branch
  (BUG-0766) was dead for that case.
* The first byte 5 is also the superweapon request of a Civ guest (u8 5, u32 type, 3 x f32: 17 bytes; CMD_SPACE_BUILD
  is 33 bytes), which must stay in the Civ hold while the host is in a Civ editor: routing by the byte alone would take
  it into the Space hold.

## Fix
civ.cpp: outside Civ the host routes a MSG_CIV_CMD to `host_space_build` while in Space (as before) or, with the
fix, when it is the 33-byte CMD_SPACE_BUILD in any other mode (`civlogic::space_build_routed`, civ_logic.h, the sizes
`kSpaceBuildBytes` / `kSuperRequestBytes`); `host_space_build` keeps it (`space_hold`, with the world of the save) until
this game is on that planet, as for a host whose ship is away (BUG-0766). No wire change. Switch `coop.fix
civ.space_build_route off`: 0.7.16.1.

## Verification
open, waiting for the lab run. pending night run: space_planner_menu, space_planner_menu_off (space_empire, about
9 min each, one lane). `coop_space_planner_build.py --menu`: the host saves and goes to the galaxy menu (`coop.menu`),
the guest buys a house in its colony's planner with real input, the host loads the same save (`coop.load`) and lands
on the home planet, the test frees a plot as --away does. PASS: the host's city has the guest's house (the game's
building list). Twin `space_planner_menu_off` (SPORE_FIX_OFF=civ.space_build_route) must FAIL at "the host's game has
the guest's new house". Not covered by a run: the captain's outfitter (it did not open in the lab saves,
docs/re/space.md#space-captain-screen-fault); the galaxy menu and the load are the same route (a mode other than Space
and Civ).
0.7.17 release lab: PASS: space_planner_menu (lab 20261124-020506), unit (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-V1), branch fix/w78-civ.
- 2026-11-23: fixed on fix/w78-civ (9b9fdf4a), switch civ.space_build_route; lab test and twin written, night run pending.
- 2026-11-24: released in 0.7.17.
