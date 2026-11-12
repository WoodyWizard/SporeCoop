---
id: BUG-0935
title: Tribe: the guest's roster shows the members red with an empty health bar while the host's roster has them healthy (play_tribe, campaign)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-090547
role: guest
versions: 0.7.12 (unreleased)
fixed_in: unreleased
branch: fix/w48-tribe-mirrors
commits: 6fc65d20
tests: tribe_mirror_hunger
signature:
docs: docs/re/tribe.md#tribe-mirror-looks, docs/06-testing.md#testing-tribe-mirror-looks
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
Proven (Ghidra + runtime). A member's hunger is cCreatureBase +0xBBC (mHunger, 0..100); its update 0xC0D5A0 takes
0.28 a second off it and below 1 the member starves (TakeDamage of its combatant every frame); a member eats in its
behaviour tree (0xC22C40). A guest's mirror runs no tree, so it never ate: probe on lane 4 (the new `hunger=` of the
`coop.tribe` rows) host 99.8 / guest 30.3 after 4 min, guest 0 after about 6 min, while health stayed the host's
(the snapshot). The roster card of a starving member is red with an empty bar: the guest's cards after minutes of
play. Candidate (2) of the report (the health the card reads) was not it: `maxhp=` and `hp=` agreed.

## Fix
`tribe.mirror_hunger`: the member's hunger is a replicated field (fields kind `tribe_member_hunger` 11, field 7),
applied with the game's clamped setter 0xC0BB30 (`SporeCoop::Creature_SetHunger`) when the mirror is 2 % off. Lab
logs: "member #1's hunger 66.1 -> 99.9 as in the host's game" after the host's member ate.

## Verification
Test `tools/tests/coop_tribe_mirror_hunger.py` (suite `tribe_mirror_hunger`): the guest's mirrors set to hunger 0
(what minutes of its own update leave), the host's member to 40: within 5 s every mirror has the host's value; key
frame `roster`.
* FAIL with `SPORE_FIX_OFF=tribe.mirror_hunger`: out/lab/20261112-091714 (mirrors stay at 0, the host's 38.5 not
  mirrored).
* PASS: out/lab/20261112-090616-2, out/lab/20261112-094224.

## History
- 2026-11-11: opened.
- 2026-11-12: fixed (fix/w48-tribe-mirrors): hunger replicated.
