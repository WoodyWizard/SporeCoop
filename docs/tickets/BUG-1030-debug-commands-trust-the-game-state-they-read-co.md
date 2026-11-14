---
id: BUG-1030
title: Debug commands trust the game state they read: coop.abilities lift at r 0, unchecked symbols and pointers in coop.cell / cam / w2s / dna / savedb / loadhook / test_view, terrain reapply entries
status: fixed
kind: tooling
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-24
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-ui
commits: 8bc5ebf9, 66f43dd7
tests: make test (re_lint; no-fail-run)
signature:
docs:
related:
switch: none (debug commands)
---

## Report
Audit 2026-11-12 for 0.7.14, finding SPC-24 (a bag of debug paths). Not seen at runtime, found by code reading. Debug
and test console commands that read game state without checking it; only a developer or a test typing them can
reach them. One ticket because they share one cause (debug commands trust their input and the build's symbols).

## Root cause
Checked one by one on main efd2e0ba:
* view_centres.cpp:124 h_Relevance "8 bytes instead of 12": NOT a bug and not a debug path. The 8-byte check guards
  exactly the two pointers read at :125..:126; v[2] at :132 is covered by the 12-byte check at :120 on the same
  manager. Audit wrong.
* sui.cpp `coop.ui align|area|caption` raw strtoul -> Win: NOT a bug, already fixed by BUG-0812: all three go through
  arg_win (sui.cpp:509, :623, :633, :641), which takes the number only when the live UI tree has it. Audit wrong.
* debug.cpp:581 `coop.abilities lift`: k = (r + m) / r with r = |position| = 0 divides by zero (a position at the
  planet's centre is not a real case). Confirmed, trivial.
* debug.cpp:734 c_sample "no teb[2] StackLimit": NOT a bug: the copy runs from the live Esp up to StackBase
  (teb[1]), memory that is always committed; StackLimit (the low end) is not needed. Audit wrong.
* c_savedb (debug.cpp:950), c_loadhook (:961), c_cam / c_w2s / c_cell / c_dna, test_view.cpp:44..:134 call
  H->sym(...) results without a null check. Confirmed in code; unreachable on the supported build (tools/re_lint.py
  fails the build when a name is missing from overrides_dc04aee5.sym), reachable only on an unknown build.
* terrain_diag.cpp:191 `reapply`: checks the list head (+0x770, 8 bytes) but reads entries bg + i*0xAC (:210..:212)
  without skg::readable. Confirmed, RE tool only.
Severity: Low, debug and test commands only; no player path.

## Fix idea
Guard r < 1e-3 in lift; a helper `need_sym(name, out)` in debug.cpp that prints "no symbol" and returns 1;
skg::readable over the reapply range. No twin switch needed (debug tools).

## How to test
No twoinst test can show a player effect. `no-fail-run: debug commands, hardening only`; a smoke test of each
command with bad input in tools/tests/coop_ui_bad_arg.py style (lift on a stubbed zero position is not reachable).

## Fix
debug.cpp: need_syms() checks the symbols before c_cell (CreateCellObject), c_cam, c_w2s (also a null mode
manager / viewer), c_dna add / gain, c_savedb and c_loadhook call them; `coop.abilities lift` refuses a position at the
planet's centre (r < 1e-3, a division by 0); test_view.cpp `coop.view set` checks its four viewer symbols before it
hooks; terrain_diag.cpp `reapply` checks the whole range of entries it reads (skg::readable). Not bugs (the audit was
wrong, see Root cause): h_Relevance, coop.ui align/area/caption (BUG-0812), c_sample. c_loadworld is left to the K2
package (its thread flag, BUG-1018 family). No switch: debug and test commands only.

## Verification
* no-fail-run: debug and test commands, hardening only; the unchecked symbols are unreachable on the supported
  build (re_lint fails the build when a name is missing) and lift at r 0 needs a position at the planet's centre.
  make test (re_lint) passes; the commands still answer in the lab tests that use them.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-ui (0.7.14 P2).
- 2026-11-14: released in 0.7.14.
