---
id: BUG-1087
title: Tribe: a click on another player's member (or its roster card) is silently dropped: no word whose member it is
status: open
kind: bug
stage: Tribe
reported: 2026-11-13
source: Discord player report 2026-11-13 + forwarded analysis
role: guest
versions: 0.7.x
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/09-coop-design.md, docs/re/tribe.md
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

## History
- 2026-11-13: opened.
