---
id: BUG-1481
title: Civ: the guest's orphan sweep removes a local candidate that still waits for its design answer (part a) (CIV-15)
status: open
kind: bug
stage: Civ
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w78-civ
commits: 9b9fdf4a, 9479fa44
tests: civ_design_kept, civ_design_kept_off (tools/tests/coop_civ_host_design.py --kept); unit tests/civ_tests.cpp test_audit_v1
signature:
docs: docs/re/civ.md#civ-audit-v1-0717
related: BUG-0647, BUG-1140
switch: civ.wait_design_kept
---

## Report
External static audit of 0.7.15.5, item CIV-15 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-V1 (fix/w78-civ). A behaviour change gets a switch and a twin.

## Root cause
* Code (rel/0.7.17, civ_guest.cpp `guest_state`): a local vehicle chosen as a mirror's candidate whose design answer
  is pending is skipped (`if (v && !bound_design(v)) continue;`, "bound on a later cycle", BUG-0647), but the same
  complete cycle's sweep of the guest's own vehicles (`!gv_bound(lv)` -> `kill_local`) removed it; a new mirror was made
  on a later cycle. With one chunk per cycle (the usual case) the "later cycle" never came for the first vehicle of a
  design: the vehicle vanished and came again (cosmetic).
* The design answers are forgotten at every session end (creations on_session_end), so a guest's join or rejoin meets
  this for every loaded vehicle of the host's own designs. Static reading.

## Fix
civ_guest.cpp: the candidates of a cycle that wait for their design answer are kept in a set (pointers compared only,
at most 256, swapped out at each complete cycle's sweep); the sweep spares them (`civlogic::sweep_spares`,
civ_logic.h) and the mirror binds the same vehicle once the answer came. Counters `wait_design` / `wait_design_spared`
(g_n). The vehicle lines of `coop.civ` print the game's object serial (`serial=`, tests). Switch `coop.fix
civ.wait_design_kept off`: 0.7.16.1.

## Verification
open, waiting for the lab run. pending night run: civ_design_kept, civ_design_kept_off (cold start, about 5 min
each, one lane). `coop_civ_host_design.py --kept`: after the BUG-0647 part the guest holds the host's vehicle states
(`coop.civ vehhold 1`), rejoins, reads its loaded vehicles of the test design (address, serial), takes the states
again. PASS: the guest's mirror of the host's vehicle is the loaded game object (address and object serial). Twin
`civ_design_kept_off` (SPORE_FIX_OFF=civ.wait_design_kept) must FAIL at "the guest's mirror is the vehicle its game
loaded".

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-V1), branch fix/w78-civ.
- 2026-11-23: fixed on fix/w78-civ (9b9fdf4a), switch civ.wait_design_kept; lab test and twin written, night run pending.
