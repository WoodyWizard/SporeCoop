---
id: BUG-0594
title: Tribe: a totem pole on the guest only before the move to Civ (tribe_civ, detector guest_only:totem_pole)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-05
source: detector, rel/0.7.7 final check out/lab/20261105-015320 and 015350
role: guest
versions: 0.7.7-dev
fixed_in: 0.7.8
branch: fix/w44-world-state2
commits: d21a18ff 040d4314 be1f1b6b 3567b106 cd2f986f 06f729dd
tests: tribe_civ (coop_tribe_civ.py), make test (tools_selftest.py worldstate)
signature:
docs: docs/re/tribe.md#tribe-stage-end
related:
---

## Report
tribe_civ (3 samples): "1 totem_pole on the guest, none on the host (at (404 8 305))", "totem_pole: 0 in the
host's game, 1 in the guest's (whole list)". Reproduce: `SPORE_DETECTOR=20 tools/lab.py run tribe_civ`.

## Root cause
Observed (the 0.7.7 final tribe_civ 20261105-015320; lab 20261105-123325, 123819, 124141): between the City Hall
editor and the vehicle editor the host's game has destroyed its player village's totem pole and food mat (none in
its noun list, the tribe's +0x260 = 0: the game prepares the Civ stage); the guest's game, which opens no such
editor, keeps them at (404 8 305) until the Civ world replaces its planet. Neither player sees the other's world in
this phase (the host is in the cinematic and the editors). So no desync a player can see: the detector compared a
host world the game had taken down.

## Fix
coop.wstate `st phase stage_end=1` (tribe::stage_end: the player tribe without its food mat); worlddiff skips such a
sample as `apart` (SPORE_DETECTOR_STAGE_END=1 compares anyway). The guest's village is not changed.

## Verification
tribe_civ with SPORE_DETECTOR=20: FAIL (the rows) with `SPORE_DETECTOR_STAGE_END=1` out/lab/20261105-124141 (4 samples
of Tribe:planet:totem_pole, guest_only:totem_pole, noun_only_guest:totem_pole, tribe_food_mat), PASS
out/lab/20261105-123819 (no detector row; the test checks stage_end is true after the City Hall). make test:
tools_selftest worldstate "the host in the end of Tribe: not the same place" fails against the rel/0.7.8
worlddiff.py, passes here.

## History
- 2026-11-05: opened.
- 2026-11-05: the detector skips the host's end of Tribe (fix/w44-world-state2).
- 2026-11-05: released in 0.7.8.
