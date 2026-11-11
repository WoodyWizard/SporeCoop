---
id: BUG-0939
title: Tribe: right after Creature -> Tribe the arrow of a guest that has no chief yet points to the planet's centre ('Dave 504 m')
status: open
kind: bug
stage: Tribe
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-090547
role: host
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0274, BUG-0339, BUG-0907
---

## Report
Key-frame review of the second final check out/lab/20261111-090547 (rel/0.7.12 94ca59ab).
Sheet: `out/lab/20261111-090547/frames/sheets/005_campaign_04_advance-creature.jpg`. Frames: `out/lab/20261111-090547/frames/campaign/04_advance-creature_host-Alice.jpg`,
`04_advance-creature_guest-Dave.jpg` (campaign, 4 players, the frame right after the Creature -> Tribe change).

* Host: at the bottom edge an arrow in Dave's colour with "Dave 504 m", pointing down. The village shows three
  chiefs ("Alice's chief", "Carol's chief", "Bob's chief"); there is no chief of Dave.
* Dave's screen: the same village and three chiefs, none of its own; its roster has two cards.
* The manifest has Dave's `coop.avatar` position as (0.0 0.0 0.0); the host is at (129.3 -471.8 -119.8), 504 m
  from the planet's centre: the arrow points at the centre of the planet.
* At the end of the Tribe play (sheet 006) Dave has a position and a chief, so the state passes.

Severity: low (a wrong arrow for some seconds after a stage change). Confidence: high for the arrow and its
distance; how long it lasts is not known (one frame). Regression of 0.7.12: unknown (the arrows are from 0.7.10,
find_partners).

## Root cause
Inferred (high for the arrow, the rest not analysed): the partner arrow takes the reported position of a player
as it is; a guest that is in the stage but has no chief yet reports (0 0 0), and the arrow code does not treat
that as "no place". The same value has been a marker of "no object" before (BUG-0274, BUG-0339). Whether Dave's
chief should be there by the time the other three are is a second question (the campaign's check of the phase
passed).

## Fix
Open.

## Verification
Open. How to test: campaign_tribe or find_partners_tribe with a guest whose chief is made late: while the guest's
state has no avatar (`coop.status`: avatar=no or pos 0 0 0) the host must draw no arrow and no tag for it
(`coop.find` rows). A unit check: the arrow function with a zero position returns "hidden".

## History
- 2026-11-11: opened.
