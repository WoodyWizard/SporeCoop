---
id: BUG-1219
title: Visual: the Creature status block's "Pack: ..." line runs off the right screen edge
status: open
kind: bug
stage: Creature
reported: 2026-11-16
source: plan
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
