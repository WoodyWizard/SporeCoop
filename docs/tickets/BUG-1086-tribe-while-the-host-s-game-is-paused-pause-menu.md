---
id: BUG-1086
title: Tribe: while the host's game is paused (pause menu, Sporepedia, a modal) a guest's orders wait without a word and its members stand
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
docs: docs/re/tribe.md, docs/09-coop-design.md
related: BUG-0960, BUG-1081, BUG-0531, BUG-0983, BUG-0313, BUG-0151
---

## Report
A Discord player (0.7.x, version unknown) says the guest cannot control its units in Tribe. The forwarded analysis
says a host's pause (Esc menu, P, Sporepedia, a tutorial box) leaves the guest unpaused, with no note, while its
orders wait for the host's paused game. Checked on rel/0.7.14 by reading the code (no run).

## Root cause
Observed in code, the player effect inferred:
* In Tribe every guest member is a mirror: the guest's order goes to the host (tribe.cpp h_WalkSelected 818-840,
  h_DoActionForObjects 695-720), the host's game runs it with its behaviour trees and the guest's mirror follows the
  host's states.
* While the host's game time stands (pause menu, Sporepedia, My Collections, a tutorial or other modal) the host's
  members do not act and its states do not move: the guest's orders are taken and nothing happens until the host
  goes on.
* The guest is never told: PlayerState has no pause bit (mods/coop/net/protocol.h:173-180) and tribe.cpp holds the
  guest's game only for the host's game-over caption (BUG-0960), the hut-destroyed caption (BUG-1081, 0.7.14) and the
  village captions (BUG-0531 / BUG-0983 close them after 8 s). No other host pause is shown on the guest
  (coop_ui.cpp, notify.cpp: nothing).
* Not the same: BUG-0313 is the guest's own game time paused ~7 s after its world load (game-bug). The analysis'
  "bake pause at Tribe start" names BUG-0313, but that ticket is about the guest.

For an ordinary 2-player session this explains "my orders do nothing for a while" (as long as the host sits in a
menu), not a permanent loss of control.

## Fix idea
The host sends its pause (game time standing, cGameTimeManager +0x48 bit 0, without the co-op window's own pause)
as a state bit or message; a guest in the host's world (Tribe, Civ, also Creature for the NPCs) shows "<host> paused
the game" and either pauses its own game as tribe.gameover_wait does (BUG-0960) or at least says that its orders run
when the host goes on.

## How to test
Tribe fixture, two players: the host opens the pause menu (Esc) and keeps it 20 s; the guest orders one of its
members to walk 30 m. Assert on the guest's screen and game: a note about the host's pause is shown (today none:
FAIL) and, after the host closes the menu, the member walks. Key frames of both games during the pause.

## History
- 2026-11-13: opened.
