---
id: BUG-0646
title: Tribe: the guest does not hear the music of the dance around the fire
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-08
role: guest
versions: 0.7.8
fixed_in: 0.7.10
branch: fix/w46-tribe-dance
commits: c9265c9a, 865501d9, 8e1f4c48
tests: tribe_party_music
signature:
docs: docs/re/tribe.md#tribe-party-music
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

Observed 2026-11-07 (fix/w46-tribe-dance, Ghidra + lab lane 4): the dance at the fire is the game behaviour
cTribeGameBehaviorParty (CitizenAction 12 Party on the own fire pit; factory 0xD791C0, vtable 0x147BEB0). Its tick
0xD793F0 walks the members to slots round the fire pit and, once all stand there, plays
Audio::PlayProceduralAudio(0x19DC06DF, a new track, the fire pit's position) (call 0xD79E7C, return 0xD79E81; the only use
of that sound); its exit 0xD78F80 stops it (Audio::StopAudio). Not the theme manager, so BUG-0568's relay never saw it. The
behaviour runs in the host's game only (the guest's members are mirrors, its orders go to the host). Lab 20261107-102952:
`coop.tribe party` on the host's own members: tribe +0x554 (partying) 1 for ~25 s, the host's game played 0x19DC06DF, the
guest's none (the dance animations were mirrored). The earlier attempt's party did run too ("ret 0" is the normal answer);
it looked for theme plays only. docs/re/tribe.md#tribe-party-music.

## Fix
`tribe.party_music` (mods/coop/tribe.cpp): Audio::PlayProceduralAudio and Audio::StopAudio hooked; the host relays a play
of 0x19DC06DF and the stop of its track to the guests (MSG_NPC_ANIM kinds 7 / 8, reliable); a guest plays the same sound on
a new track of its own at the place (its own volume and settings) and stops it with the host. `coop.fix tribe.party_music
off`: nothing sent or played.

## Verification
Test tribe_party_music (coop.audio of each game, the party ordered as a click on the fire pit does):
FAIL with SPORE_FIX_OFF=tribe.party_music out/lab/20261107-103745 (the guest's game played nothing),
PASS out/lab/20261107-103611 (the guest's game played 0x19DC06DF 3 s after the host's and stopped with it 21 s later).
Not verified: a guest joining during a party (the play is sent once), the night dance of the report's moment.

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07: not reproduced in fix/w46-tribe-civ; footsteps found host-only: BUG-0669.
- 2026-11-07: reproduced and fixed in fix/w46-tribe-dance (`tribe.party_music`).
- 2026-11-07: released in 0.7.10.
- 2026-11-10 (plan/w47-closed-fixed): fixed-ticket review: the relayed stop is lost when either game is outside Tribe mode at that moment (tribe.cpp:1510-1515, 1581), so the guest's music can play on; new ticket BUG-0890.
