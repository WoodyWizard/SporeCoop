---
id: BUG-0649
title: Civ: the guest does not see the host's name at the host's camera (the host sees the guest's)
status: not-reproduced
kind: bug
stage: Civ
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): text only (Discord 2026-11-06)
role: guest
versions: 0.7.8
fixed_in:
branch: fix/w46-tribe-civ
commits: c9265c9a
tests: civ_cursor
signature:
docs: docs/re/civ.md#civ-partner-tag
related: 
---

## Report
Discord report 2026-11-06 (0.7.8), text only: in Civ the host sees the guest's name at the guest's camera, the guest
does not see the host's name.

## Root cause
Observed (code): in Tribe and Civ the partner is a Marker (modes.cpp `rts_*`) where the partner's mouse ray hits the
planet, with a name tag. Observed (the guest's log): `partner 0 'Sterben' Civilization ... puppet=shown` with drawn
positions jumping up to 154 m/s: the guest's game has the host's marker. The guest's s 5930 shot has a green label with
the host's name on the ground (maybe that marker).
Inferred, not determinable from the logs: the tag is drawn but not noticed (small, or under the host's vehicle labels,
or jumping), or hidden on the guest in some cases.

Observed 2026-11-07: the guest's own screenshot of the report (s 5930) shows the host's green name tag on the guest's screen at the host's vehicles (where the host's mouse was), among the blue owner tags of the vehicles with the same name. The code draws every remote player's tag the same way on host and guest (coop.cpp on_overlay, Marker at the partner's mouse point, modes.cpp rts_fill). Not a bug: the tag is at the partner's mouse, not its camera, small, and it jumps with the mouse; the guest did not notice it.

## Fix
None (works as designed). A larger or camera-anchored tag would be a feature request.

## Verification
no-fail-run: not a bug; the evidence is the report's own screenshot and the code path (docs/re/civ.md#civ-partner-tag); the pointer's place on the other game is covered by civ_cursor (BUG-0380).

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07: closed in fix/w46-tribe-civ: the tag is drawn (the guest's own shot shows it).
