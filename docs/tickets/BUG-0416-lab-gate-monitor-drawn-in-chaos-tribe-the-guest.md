---
id: BUG-0416
title: Lab gate: monitor drawn in chaos_tribe, the guest's copy of the host's pointer at rest 164 m from the host's own
status: fixed
kind: test
stage: Tribe
reported: 2026-11-01
source: lab 20261101-140615 chaos_tribe, lane 7 (fix/w33-proxy-ground)
role: guest
versions: unreleased
fixed_in: 0.7.3.4
branch: fix/w35-monitor-gates
commits: e64551b, f19d618
tests: monitor_tribe, chaos_tribe, make test (gates_selftest.py)
signature:
docs: docs/06-testing.md#testing-monitor-w35, docs/REPORTING.md#reporting-monitor-fields
related: BUG-0393, BUG-0380, BUG-0415
---

## Report
out/lab/20261101-140615-fix-w33-proxy-ground chaos_tribe FAILED on `drawn: guest draws host 164.2 m from where host's
own game has it (limit 3.0 m, Tribe) on 2 lines from s 90`. The guest's partner lines at s 90 / 100 say `drawn_v=0.0`
(at rest) with drawn=(-168.1 -426.5 215.6) and (35.6 -424.2 274.7), the host's own pos= (332.2 -178.3 338.3) and
(125.9 -468.8 145.0): the chaos bot moves the host's view / pointer far between the marks (minimap clicks), rtt up to
193 ms on those lines. 3 + 4 later chaos_tribe runs on lane 7 did not show it.

## Root cause
Decided (fix/w35-monitor-gates): the rule judged a pointer that had just jumped, no copy is wrong. `drawn_v` is the
speed over the newest states the guest has (`r.buf.sample(now)` and `now - 0.5`), not the drawn copy's: a copy
looks "at rest" (drawn_v 0) while no state with the pointer's new point has arrived yet. In 20261101-140615 the
host's 825 ms save hitch ("Tribe, saving", s 99.1-99.93) held its states back while the chaos bot moved the mouse;
the host's pointer jumped in its first frame after it, its trace line at s 100.0 has the new point, the guest's
(same mark, rtt 193 ms) the copy at the old one. The sync-check at s 99.1 had compared both pointers (p2) OK, and
at s 80 and s 110 the guest drew the host's pointer exactly where the host had it (0.0 m). s 90 cannot be
decided from the old logs (no field said when the pointer last moved); the guest had rejoined at s 87.1.
New runs with the field: chaos_tribe out/lab/20261101-174833 #2 / #3 had single lines with a resting copy 16.5 m
and 42.5 m off, each with the host's `ptr_still=0.0` (just moved), none with a pointer that had stood still.

## Fix
* Mod (trace only, no behaviour change): the own `trace:` line in Tribe / Civ ends with `ptr_still=<s>`, the
  seconds since the own pointer marker last moved (modes::rts_marker_still, the moved_at of BUG-0380's
  diag.rts_cursor).
* Rule (tools/tests/monitor.py `Drawn`): a Tribe / Civ copy is judged only when its player's own line says the
  pointer stood still for `drawn_still` (1 s) + the line's rtt (as well as drawn_v <= 0.5 as before). Logs without
  the field are judged as before; `drawn_still < 0` (SPORE_MONITOR) is the rule before.
* Test `monitor_tribe` (coop_monitor_tribe.py): the misjudgement reproduced on purpose, and a planted real
  problem.

## Verification
* FAIL (the rule before, SPORE_MONITOR='{"drawn_still": -1, "share_after": -1}'): monitor_tribe
  out/lab/20261101-173952: the host (600 ms frames) pins its pointer 176 m away in its first frame after the marks
  s 40 and s 50; the guest's lines have the copy 175.9 m off, drawn_v 0.0, the host's ptr_still=0.0:
  `MONITOR-VIOLATION drawn guest: guest draws host 175.9 m ... on 2 lines from s 40`.
* PASS: monitor_tribe out/lab/20261101-174232 (the same jumps on 3 marks, 175.9 m, drawn_v 0.0, ptr_still=0.0: no
  drawn violation); chaos_tribe 3/3 out/lab/20261101-174833; smoke out/lab/20261101-180732 (12 game tests OK; unit failed on this branch's ticket index, then in progress; ui skipped: exclusive lanes 1-5).
* Planted, the rule still fires: monitor_tribe 20261101-174232 (`coop.puppet hold on` on the guest, the host's
  pointer moved once and resting: `drawn guest: ... 346.5 m ... on 2 lines from s 70` after 21 s); gates_selftest.py
  `f_drawn_ptr` (a resting pointer 8 m off) fires, `f_drawn_jump` quiet, `f_drawn_old` (no field) fires.

## History
- 2026-11-01 (fix/w35-monitor-gates): decided: the rule (a jumped pointer's copy before its new state); ptr_still= and the rule fixed.
- 2026-11-01: opened.
- 2026-11-02: released in 0.7.3.4.
