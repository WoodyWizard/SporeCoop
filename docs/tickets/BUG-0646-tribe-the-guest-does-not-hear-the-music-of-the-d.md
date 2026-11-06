---
id: BUG-0646
title: Tribe: the guest does not hear the music of the dance around the fire
status: open
kind: bug
stage: Tribe
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-08
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0568
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/Sterben-08 (the host, s 5218): "the guest doesn't hear the music
which plays during dancing around the fire".

## Root cause
BUG-0568 (fixed 0.7.8, `tribe.theme_music`) relays the theme manager's play / move / stop (hunt and fight theme
0xAD1A17D). Observed: no audio / music / social / dance lines on either side at s 5100-5220 (expected: the audio log is
a lab tool).
Inferred: the dance music is not played through the theme manager but by the social performance of the host's members
(performance 0xB46E8803, instrument tools 4/5/6, docs/re/tribe.md); the guest's members are mirrors without a
behaviour tree, so the 0568 relay does not cover it. Not a regression of BUG-0568.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: `coop.audio` recording on both games while the host's tribe dances at the fire (a celebration or the
     instrument social); the sound ids on the host that the guest lacks. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
