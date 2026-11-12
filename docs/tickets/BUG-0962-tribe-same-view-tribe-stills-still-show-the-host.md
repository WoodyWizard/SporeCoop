---
id: BUG-0962
title: Tribe: same_view_tribe stills still show the host chief's staff held differently on the guest in 2 of 4 views with tribe.mirror_hold on (phase or hold timing)
status: open
kind: bug
stage: Tribe
reported: 2026-11-12
source: fix/w48-tribe-mirrors, same_view_tribe key frames lab 20261112-102606
role: guest
versions: 0.7.13 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0914, BUG-0936, BUG-0569
---

## Report
Key-frame review of same_view_tribe in lab 20261112-102606 (fix/w48-tribe-mirrors, BUG-0914's tribe.mirror_hold and
BUG-0936's tribe.mirror_idle on): the places and the members' poses agree, but "Alice's chief" (the host's) holds its
staff forward / low in the hand on the host and upright on the guest in view 1 and view 3, the other way round in
view 4. tribe_mirror_hold (the chief's hold in its animation, 3 rounds) passes 3 of 3 and its key frame shows the staff
in the hand on both.

## Root cause
Not analysed. Inferred: the chief's gestures (0x04C6CB02 / 05) are short holds; a still catches the host and the
mirror at different phases of the same animation (the relay starts it 0.1-0.3 s later) or one in the hold and the
other just out of it. Measure the time from the host's hold start / end to the mirror's (both games' `coop.tribe
anims` hold= at 20 Hz) before changing anything.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
