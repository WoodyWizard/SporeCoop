---
id: BUG-0946
title: Tribe: a member thrown up by a hard blow (about 5 m, 1.5 s) stays on the ground on the guests' screens
status: open
kind: desync
stage: tribe
reported: 2026-11-11
source: fix/w47-final12c, probes and tribe_member_air (lab 20261111-182334)
role: guest
versions: 0.7.11 - 0.7.12
fixed_in:
branch:
commits:
tests: tribe_member_air prints it (not asserted)
signature:
docs: docs/re/tribe.md#tribe-member-toss
related: BUG-0945, BUG-0329
---

## Report
Found while looking for BUG-0945's cause (no player report). On the host a tribe member that takes a hard blow is
thrown up: about 5 m high, in the air for 1.5-3 s. On every guest the same member stays on the ground for that time.

## Root cause
Observed (lab 20261111-182334, tribe fixture): after blows of 60, 100 and 140 hp on the host's leader the host has it
4.96-5.04 m over the ground; the guest's mirror is at most 0.03-0.06 m over its own ground in the 3 s after each
blow. A guest's mirror keeps its own height (drive.own_height: BUG-0329, and tribe.mirror_ground, BUG-0444) because
the two games' grounds can differ; the host's height over its ground is not sent for members.
Not checked: whether the mirror plays the hit animation, how it looks next to the attacker's copy.

## Fix
None yet. A way: send the member's height over the host's ground while it is up (as the animal proxies' trailer,
BUG-0550) and add it to the mirror's own ground.

## Verification

## History
- 2026-11-11: opened (fix/w47-final12c). For 0.7.13.
