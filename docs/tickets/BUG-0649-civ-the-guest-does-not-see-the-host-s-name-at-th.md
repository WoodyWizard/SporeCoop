---
id: BUG-0649
title: Civ: the guest does not see the host's name at the host's camera (the host sees the guest's)
status: open
kind: bug
stage: Civ
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): text only (Discord 2026-11-06)
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
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

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: play_civ; read on both games whether the partner marker's tag is drawn, where and how big. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
