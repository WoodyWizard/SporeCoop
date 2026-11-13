---
id: BUG-1069
title: Tribe: same_view_tribe stills: the host's chief holds its staff in the hand, the guest's copy has it in the resting grip (view 4) or held another way (view 1)
status: open
kind: desync
stage: Tribe
reported: 2026-11-13
source: fix/w49-mirror final check, lab 20261113-051820-2 same_view_tribe
role: guest
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
Key frames of fix/w49-mirror's final check (out/lab/20261113-051820-2, sheets 005 / 008 same_view_tribe view 1 / 4),
after BUG-0972 (a returned gesture) and BUG-0988 (the animation's moment) were fixed: view 4: the host's chief ("Alice's
chief") holds its staff in the hand, slanted forward; the guest's copy has it upright in the resting grip. View 1: the
staff upright in the hand on the host, slanted down with the blade forward on the guest. The tests of the hold
(tribe_hold_guest x2) and the phase (tribe_mirror_phase) passed in the same runs, so this is a moment the hold relay
(BUG-0914 / 0971 / 0968 / 0992) or the item's grip differs at the still, not the animation's phase. Not analysed.
Reproduce: `lab.py run same_view_tribe --repeat 3` and compare the chief's staff in the stills; sample `coop.tribe anims`
(`hold=`, `t=`) of the chief on both games at the still's moment.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
