---
id: BUG-9096
title: Lineages internet session stalls and repeatedly transfers the world
status: open
kind: bug
stage: Cell
reported: 2026-11-07
source: Player report archive, 22:37 local
role: host guest
versions: 0.7.3.6-lineages27-dev
fixed_in:
branch: fix/lineages-player-session
commits: 75b3901b
tests: tools/tests/lineages_cell_editor_report.py; tools/tests/lineages_cell_first_hit.py
signature:
docs: docs/re/cell.md#cell-lineages-player-report
related: BUG-0405 BUG-0343
---

## Report

Initial download appeared stuck at zero, then completed slowly after a menu
visit/manual share. The client repeatedly downloaded the world, saw different
gameplay, missed an editor notice, and received delayed repeated food notices.
The player also questioned the scaled workload and editor spending budget.
Follow-up: the first hit appeared not to reduce enemy health until another hit;
the guest also saw nearby objects disappear. Delayed authoritative state is a
candidate explanation, not a confirmed cause without guest-side evidence.
One expected library creation appeared later; this is delayed sharing, not a
confirmed missing import. No private names, addresses or save paths are retained.

## Evidence

Both peers report Lineages 27 on Steam 3010a413. No caught faults. Automatic
sharing did start: first 2,782,805-byte transfer at session 85.969 seconds, queue
completion at 625.783 seconds (539.8 seconds, 5 KiB/s). Reported packet loss is
mostly 80-99%, with large retransmission counts; this statistic is acknowledgement
evidence, not proof of physical link loss. Second share at 706.891 seconds;
returning from Cell editor starts a third at 1225.790 and replaces the second.
The host's editor vote was emitted at 1100.633 seconds while transfer backlog
remained. Reserved founders=4, difficulty=100%, hence workload=400%.

## Investigation

Port upstream BUG-0405 acknowledgement/retransmission changes selectively;
reproduce with its deterministic slow-receiver tests before native checks.
Trace same-stage editor return before changing reload policy. Verify Cell
food and editor accounting separately; delayed notices alone do not prove
duplicate rewards. Client-side internal state is not present in this host zip.

## Implemented and qualified

Selective upstream transport and camera fixes: BUG-0405 and BUG-0343. Same-stage
Cell editor return keeps a fresh already-joined peer in its current world; the
creation stream updates the design. Guard: world.cell_editor_in_place. Negative
control: editor-control.log starts a new full share. Saved host species reached
the guest puppet without a new share (editor-verify.log); repeat cancel kept
shares at four and spent DNA unchanged (editor-fixed-second.log). Native logs
are in out/player-report-20261107-223741. Large partner arrow: BUG-9097.

The delayed-link first hit changed HP 7 -> 4 within 0.406 seconds; repeated food
totals did not duplicate awards. This does not close all first-hit visual or
object-disappearance reports. The Cell native ring uses hp/6 despite scaling.
The first saved starter model also produced a negative spent-DNA value; the
solo/editor valuation control remains missing. These uncertainties stay open
under this ticket and BUG-0351. Nayla subsequently appeared, so no missing-import
repair is made on the basis of this report.

## History
- 2026-11-07: reproduced transfer flooding, redundant editor shares and camera
  displacement; implemented focused fixes, kept unproven visual/accounting
  symptoms open. See docs/06-testing.md#testing-lineages-player-report.
- Final versioned native editor gate: editor-regression-final.log passes saved
  design and cancel returns; guest puppet key matches the new host creation.
