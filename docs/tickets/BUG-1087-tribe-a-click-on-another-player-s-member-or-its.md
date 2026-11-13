---
id: BUG-1087
title: Tribe: a click on another player's member (or its roster card) is silently dropped: no word whose member it is
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-13
source: Discord player report 2026-11-13 + forwarded analysis
role: guest
versions: 0.7.x
fixed_in: unreleased
branch: fix/w49-tctl2
commits: 4d0dd0b1, 6d8d689a, 601e7ddf
tests: tribe_foreign_click
signature:
docs: docs/09-coop-design.md, docs/06-testing.md#testing-stage-end-pause-foreign
related: BUG-0907, BUG-0961, BUG-0939, BUG-1056
---

## Report
A Discord player (0.7.x, version unknown) says the guest cannot control its units in Tribe. The forwarded analysis
says clicks on the portraits of other players' members are silently dropped. Checked on rel/0.7.14 by reading the
code (no run).

## Root cause
Observed in code, by design, the missing feedback is the bug:
* The player tribe is split between the players in turns (host_tick, tribe.cpp, `players[g_player_members_seen++ %
  players.size()]`): in a 2-player session the guest owns about half of the members, the host the rest.
  docs/09-coop-design.md (Tribe row): "you can select and command only your own".
* h_SetSelected (tribe.cpp:597-605) turns a selection of a member owned by another player into "not selected", and
  drop_foreign_selection (tribe.cpp:5439) clears such members from the own selection every tick. Neither says
  anything: a click on such a member, a box over a group, or a click on its roster card selects nothing and gives no
  note whose member it is.
* A new player who does not know the split sees "some of my members do not react", which matches the report's
  wording. Members with owner 0xFF (not in the guest's tables, e.g. before the first host state) are selectable.

Not a bug (checked): the analysis' WLD-14 point (h_SetSelected / drop_foreign_selection without !local_play()) was
judged in docs/plan-0.7.14.md (WLD-14: "only h_KeepGround lacks the local-play check; the selection rule ... right",
BUG-1056): the host's members stay the host's while it is away. The analysis' "4 players get 3 members, player 4
gets 0 (BUG-0939)" is not what BUG-0939 says (an arrow to the planet's centre; that guest's roster had two cards).

## Fix idea
When a click or a box would select another player's member, show a short note once per few seconds ("this is
<name>'s member; yours have your colour / tag") and make the own members easy to tell apart (the BUG-0907 tags, the
roster cards of others greyed or hidden).

## How to test
Tribe fixture, two players: the guest clicks a member the host owns (by `coop.tribe` ids) and its roster card;
assert on the guest's game that a note is shown (today none: FAIL) and that the member stays unselected; then a
click on an own member selects it.

## Root cause, observed (2026-11-13, fix/w49-tctl2)
With `tribe.foreign_note` off: the guest's real click on the host's member is refused by h_SetSelected (`foreign
clicks 1`), nothing is selected and no panel comes (lab 20261113-154950).

## Fix
`tribe.foreign_note` (tribe.cpp): h_SetSelected counts the refused selections of a frame with their owner; the tribe
tick tells them once, at most every 5 s: "That member is <name>'s: you lead only your own members" ("Those members
are ..." for a box). Clicks, boxes and roster cards all pass SetSelected. `coop.status` prints `foreign clicks N told
M`. Off: silent (0.7.13).

## Verification
tribe_foreign_click: FAIL off (20261113-154950: refused 1, no panel); PASS x3 (20261113-154426 had the right panel,
failed only on the test's toast regex cut at the apostrophe; 20261113-154637 #1 #2 PASS); a box over an own member
still selects it. Key frame 20261113-154426 checked: the panel "That member is Alice's: you lead only your own
members" on the guest's Tribe.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-tctl2 (`tribe.foreign_note`).
