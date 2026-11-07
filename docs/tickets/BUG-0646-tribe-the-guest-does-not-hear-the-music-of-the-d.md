---
id: BUG-0646
title: Tribe: the guest does not hear the music of the dance around the fire
status: not-reproduced
kind: bug
stage: Tribe
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-08
role: guest
versions: 0.7.8
fixed_in:
branch: fix/w46-tribe-civ
commits: c9265c9a
tests: tribe_theme_music
signature:
docs: docs/re/tribe.md#tribe-dance-music
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

Observed 2026-11-07 (lab lane 4, coop.audio on both games, 3 min with a 30 s day, 9 host members at work and at the fire): the host's game played no theme music at all (the tribe.theme_music relay of BUG-0568 sends every theme play), the members' animations were the same on both games (tribe.mirror_anims), and the only host-only sounds were footsteps (BUG-0669). The dance with music the host saw (night, a conquer goal open, an epic coming) did not happen in the lab.
A second attempt (lab lane 5): the host's 5 own members ordered to party at the fire pit (`coop.tribe party`: CitizenAction 12 Party with the type 10 tool, as a click on the fire pit; DoActionForObjects returned 0), 30 s of audio: no music on the host either (only footsteps differ). ? The party may need the night or food.
What is missing to fix it: the trigger of the fire dance and the path its music takes (not the theme manager in the samples; not an animation sound of the members). Needed: one host run where the dance happens (a night at the village with the goal of the report's moment) with `coop.audio` on both games, then the sound ids the guest lacks and their callers.

## Fix
None in this release: the cause is not known (see the root cause: what is missing).

## Verification
no-fail-run: not reproduced (the dance did not occur in the lab); the theme music relay is covered by tribe_theme_music.

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07: not reproduced in fix/w46-tribe-civ; footsteps found host-only: BUG-0669.
