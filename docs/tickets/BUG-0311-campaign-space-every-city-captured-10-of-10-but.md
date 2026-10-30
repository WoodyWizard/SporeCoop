---
id: BUG-0311
title: campaign_space: every city captured (10 of 10) but the end of Civ (spaceship editor vote) never came
status: open
kind: test
stage: Civ
reported: 2026-10-30
source: lab 20261030-104149 campaign_space#1
role: host
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0290, BUG-0268
---

## Report
Lab 20261030-104149 campaign_space#1 (main 813f8cd, lane 2, 4 players, civ_npc fixture): the
civ -> space shortcut (`coop_campaign.py` kick_civ) captured one city of another nation every 30 s
(9 of other nations listed ... 1 listed), then logged "cCity::Capture of 0 city for 0x800021CA (0 of
other nations listed)" every 30 s from 11:00:46 until "the host did not reach space in 20.0 min".
The two other runs of the same test (#2, #3) logged the same "0 city (0 listed)" line 4 times each
and reached Space: after their last capture the game opened its spaceship editor (a vote "Alice
wants to edit a design (Civilization)", then Space).

## Root cause
Observed (host game log of #1): `civ(host): cities=10 owned_cities=9` at 12:00:22 (game clock),
`owned_cities=10` at 12:00:52, so every city of the planet belonged to the players: the shortcut's
"0 of other nations listed" is right, it is not blind to other nations. The 10th city changed hands
without a "captured a city" line: between those two lines the bot answered a nation's talk ("talk
answered" 11:00:37 test clock) and later closed another talk with Esc (11:01:06). After that the
game never started its end of Civ (no vote, no editor) in the remaining 14 min.
In #2 the last city was a capture ("Carol captured a city", cinematic, then the editor vote 8 s later).
Inferred, not proven: the last city went to the players through the surrender talk, not a capture,
and the game's end of Civ is triggered by the capture path (or by a talk/caption the Esc closed);
with no city left to capture the shortcut has nothing to do. Whether this is a game rule, a co-op
side effect (the talk on a guest) or the test's Esc is not known.

## Fix
Not fixed. The shortcut tool is not wrong (its count matches the game). Next step for the test:
when every city is owned and no end of Civ comes within 60 s, log `coop.ui` dialogs on every
player and the civ win state; then decide whether the shortcut needs another trigger.

## Verification
Not reproduced (1 of 3 runs in that lab, campaign_space passed 2 of 4 runs that day).

## History
- 2026-10-30: opened.
