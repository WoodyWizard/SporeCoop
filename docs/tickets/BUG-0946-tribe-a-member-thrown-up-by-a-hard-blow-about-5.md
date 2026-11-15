---
id: BUG-0946
title: Tribe: a member thrown up by a hard blow (about 5 m, 1.5 s) stays on the ground on the guests' screens
status: fixed
kind: desync
stage: tribe
reported: 2026-11-11
source: fix/w47-final12c, probes and tribe_member_air (lab 20261111-182334)
role: guest
versions: 0.7.11 - 0.7.12
fixed_in: 0.7.13
branch: fix/w48-tribe-mirrors
commits: 6fc65d20
tests: tribe_member_air
signature:
docs: docs/re/tribe.md#tribe-mirror-looks, docs/06-testing.md#testing-tribe-mirror-looks
related: BUG-0945, BUG-0329
---

## Report
Found while looking for BUG-0945's cause (no player report). On the host a tribe member that takes a hard blow is
thrown up: about 5 m high, in the air for 1.5-3 s. On every guest the same member stays on the ground for that time.

## Root cause
Proven (as observed in the report; lab 20261112-093713 with the fix off: the guest's mirror 0.00 m over its ground
after each of three blows while the host's member was 4.9-5.0 m up). A guest's mirror keeps its own height (its game's
ground, BUG-0329 / BUG-0444) and no height over the host's ground was sent for members. The thrown member keeps the
grounded byte +0x137 for part of the flight (the first try with drive::body_airborne alone placed the mirror in the
air after one blow of three: lab 20261112-090616-2).

## Fix
`tribe.mirror_air`: once a cycle the host sends every member more than 0.5 m over its ground (or off it by
drive::body_airborne), not dead, with that height (MSG_NPC_ANIM kind 9 {u16 id, f32 agl}); a guest places that mirror
at the height over its own ground with drive::air_place (not walked, not grounded, held through animation starts)
while the news is under 0.35 s old, then lets its game land it. The hold is dropped when the mirror goes, dies or the
world changes.

## Verification
Test `tools/tests/coop_tribe_member_air.py` (suite `tribe_member_air`, BUG-0945's test with a new check): after blows
of 60 / 100 / 140 on the host's leader the guest's mirror must be over 2.5 m over its own ground after 2 of 3; key
frame `toss` of kind sameview.
* FAIL with `SPORE_FIX_OFF=tribe.mirror_air`: out/lab/20261112-093713 (0.00, 0.00, 0.00 m).
* PASS: out/lab/20261112-091520-2 (2.02, 4.88, 4.52 m), out/lab/20261112-093939.

## History
- 2026-11-11: opened (fix/w47-final12c). For 0.7.13.
- 2026-11-12: fixed (fix/w48-tribe-mirrors): the members' height over the host's ground relayed.
- 2026-11-12: released in 0.7.13.
