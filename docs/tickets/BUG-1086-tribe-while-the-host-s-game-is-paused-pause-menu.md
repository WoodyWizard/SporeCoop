---
id: BUG-1086
title: Tribe: while the host's game is paused (pause menu, Sporepedia, a modal) a guest's orders wait without a word and its members stand
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-13
source: Discord player report 2026-11-13 + forwarded analysis
role: guest
versions: 0.7.x
fixed_in: 0.7.14
branch: fix/w49-tctl2
commits: 4d0dd0b1, 601e7ddf, 640ad2c1
tests: tribe_host_pause
signature:
docs: docs/09-coop-design.md, docs/06-testing.md#testing-stage-end-pause-foreign
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

## Root cause, observed (2026-11-13, fix/w49-tctl2)
With `tribe.host_pause_wait` off: the host's Esc stands its game (`busy: paused`, the pause menu up); the guest's game
runs on (`busy -`) and shows no panel (lab 20261113-154016-2, key frames of both).

## Fix
`tribe.host_pause_wait` (tribe.cpp, one mechanism with BUG-0960 / BUG-1081's game-over wait, not a copy): the host's
send cycle in Tribe turns a stand of its game time of 1 s or more (cGameTimeManager +0x48 bit 0, not a game over) into
a reason of the same wait, sent as MSG_NPC_ANIM kind 21 {up} (a new kind: a 0.7.13 guest ignores it instead of
reading it as "the tribe is out"); a guest's game stands with the game's own Pause (reason 0x4BF38A7, the one Pause of
the wait) and the panel "<host> paused the game: yours waits, your orders run when <host> goes on", and goes on with
"<host> (host) goes on: so does your game" (both notes only when the pause held 1.5 s on the guest: the host's short
stands at a join's save or a reload, seen in play_tribe and tribe_gameover_wait of 20261113-160412 as a note pair 0.1-0.4 s
apart, pause the guest without a word); a game over replaces the pause (the guest's note changes with it), the
pause's end never ends a game over's wait; the 10 s no-news and stage / session ends apply as before. `coop.status`
prints `host pauses told`. Off: the guest plays on (0.7.13).

## Verification
tribe_host_pause: FAIL off (20261113-154016-2: guest `busy -`, no panel); PASS x3 (20261113-154426, 20261113-154637
#1 #2: the guest paused 1.4 s after the host's Esc, its panel "Alice paused the game: yours waits ...", both run 0.5 s
after Resume Game). Key frame 20261113-154426 checked: the host in its Options menu, the guest's Tribe with the panel.
tribe_gameover_wait in the regression run (below).

* Regression (lanes 6 / 7): editor_now_off, vote_caption, own_editor_cancel, editor_request, evolve_in_place_creature,
  tribe_gameover_wait, play_tribe --quick PASS (20261113-160412); play_creature --quick, vote_caption PASS
  (20261113-161300); after the note delay tribe_gameover_wait, tribe_host_pause PASS (20261113-161954).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-tctl2 (`tribe.host_pause_wait`).
- 2026-11-14: released in 0.7.14.
