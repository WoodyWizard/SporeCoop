---
id: BUG-0935
title: Tribe: the guest's roster shows the members red with an empty health bar while the host's roster has them healthy (play_tribe, campaign)
status: open
kind: bug
stage: Tribe
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-090547
role: guest
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0889, BUG-0921, BUG-0643, BUG-0173
---

## Report
Key-frame review of the second final check out/lab/20261111-090547 (rel/0.7.12 94ca59ab).
Sheets: `out/lab/20261111-090547/frames/sheets/044_play_tribe_03_end.jpg`, `out/lab/20261111-090547/frames/sheets/043_play_tribe_02_mid.jpg`, `out/lab/20261111-090547/frames/sheets/006_campaign_05_play-tribe.jpg`.
Frames: `out/lab/20261111-090547/frames/play_tribe/03_end_host.jpg`, `03_end_guest.jpg`, `02_mid_host.jpg`, `02_mid_guest.jpg`,
`out/lab/20261111-090547/frames/campaign/05_play-tribe_guest-Carol.jpg`, `05_play-tribe_guest-Dave.jpg`, `05_play-tribe_host-Alice.jpg`
(the roster is the column of member cards at the right edge; crop x 915..960 of the 960 px frame and enlarge).

* Host (play_tribe end, 10 min of play): the chief's card and two member cards with the normal pale portrait, a green
  health bar and an orange hunger bar, a status bubble at the left of each card.
* Guest, the same moment, the same three slots: every card is tinted deep red, the health bar is empty (dark), no
  status bubble. The chief's card is red too.
* play_tribe mid (168 s): the host has one red card of six, the guest three red cards (the chief and two members)
  with empty bars: the two rosters already disagree.
* campaign play-tribe (4 players): the host and guest Bob have pale cards with green bars, guests Carol and Dave
  have every card red with short or empty bars.
* Not there at the start: play_tribe start, roster_portraits and tribe_portraits show the same cards with green
  bars on both games. The same picture in the first final check (out/lab/20261111-044257, play_tribe mid and end
  guest), so not an effect of the roster portrait fix (BUG-0921).
* The tests pass: play_tribe says "the same members, owners and chiefs on both"; no check reads a member's health
  or hunger on the guest.

What a guest sees: its whole tribe looks about to die (the game's red card is its alarm for a member in danger)
while the host's game has them well.

Severity: medium (wrong information in the main Tribe panel, on guests only). Confidence that it is seen: high
(3 tests, 2 runs). Regression of 0.7.12: probably not (also in the run before the BUG-0921 fix; older releases
not looked at).

## Root cause
Not analysed (review of pictures only). Observed: the guest's cards differ from the host's for the same slots at the
same moment, and the difference grows with play time. docs/re/tribe.md says "the guests see the members' health of
the host" (the blows are the host's), which the roster contradicts: either that holds for the member objects but
not for what the roster card reads, or it does not hold for every member.
Inferred (low confidence), two candidates: (1) the guest's mirrors run no behaviour tree (tribe.cpp h_BTTick,
BUG-0889), so nothing feeds them in the guest's game: their own hunger runs down to zero and the game's card turns
red and takes health off locally; (2) the health the card reads (the combatant's health against vt 0x58 maximum)
is not what the relay writes for mirrors.

## Fix
Open.

## Verification
Open. How to test: play_tribe (or tribe_coop with 5 min of idle time) and read every player-tribe member's health,
maximum and hunger on both games (`coop.tribe` rows on the host and the guest); they must agree within the relay
delay. The check the tests lack: a golden or a console read of the roster cards on the guest after minutes of
play (ui_golden has the roster only at the start).

## History
- 2026-11-11: opened.
