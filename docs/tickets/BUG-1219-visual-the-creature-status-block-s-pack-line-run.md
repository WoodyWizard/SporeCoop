---
id: BUG-1219
title: Visual: the Creature status block's "Pack: ..." line runs off the right screen edge
status: fixed
kind: bug
stage: Creature
reported: 2026-11-16
source: plan
role: 
versions: 
fixed_in: unreleased
branch: fix/w51-visual
commits: 5b0db943
tests: status_edge, tests/text_tests.cpp test_wrap_fields
signature: Pack: ... | h (cut at the right edge)
docs: docs/06-testing.md#testing-visual-0715
related: BUG-0155, BUG-0949
---

## Report
Key frame review of the 0.7.15 final check (lab 20261116-025154): in every Creature frame with a partner, on both games,
the status block's teamplay line "Pack: Alice close (regen) | rescue ready | joint hits 0, rescues 0 | h..." is cut
by the right screen edge (S/055 host_pause_creature, S/001 appeared_once, S/047 creature_brain_hp). Old, cosmetic.

## Root cause
Observed: coop.cpp draws the block at x = screen width - 480; teamplay::status() is one line of ~100 letters (891 px by
the overlay font's estimate of 9 px per letter, ~650 px drawn at 1440 x 1080): it ends ~400 px past the edge. The
block's rect (g_status_block, which keeps the labels off it, BUG-0949) did not count that line either.

## Fix
coop.cpp: the line is cut between its fields ("  |  ") into lines that end 12 px before the screen's edge
(utf8::wrap_fields, net/utf8.h, unit test tests/text_tests.cpp test_wrap_fields): two lines at 1440 px, "Pack: <partner>
close (regen)  |  rescue ready" and "joint hits N, rescues N  |  hits host N, partners N"; the block's rect counts them.
`coop.status` prints "status block (...) drawn to x <right> of <screen width>" (the widest drawn line). Switch
`core.status_wrap` (off: one line as before, the rect as before).

## Verification
Test status_edge (coop_status_edge.py, Creature, both games' `coop.status` status block line, a key frame):
* FAIL with `SPORE_FIX_OFF=core.status_wrap`: 20261116-094034 (widest line to x 1824 / 1842 of 1440; frame
  out/lab/20261116-094034/frames/status_edge/01_status_block_host.jpg: the line cut at "| hi").
* PASS: 20261116-094037, 20261116-101029 (to x 1419 of 1440; frame 20261116-094037 .../01_status_block_guest.jpg: two
  green lines inside the screen).

## History
- 2026-11-16: opened.
- 2026-11-16 (fix/w51-visual): fixed (`core.status_wrap`); test status_edge.
