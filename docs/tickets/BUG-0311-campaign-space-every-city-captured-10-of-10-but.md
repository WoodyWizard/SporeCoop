---
id: BUG-0311
title: campaign_space: every city captured (10 of 10) but the end of Civ (spaceship editor vote) never came
status: fixed
kind: test
stage: Civ
reported: 2026-10-30
source: lab 20261030-104149 campaign_space#1
role: host
versions: 0.7.3.1
fixed_in: unreleased
branch: fix/w46-g4-tests
commits: 0929b2d8
tests: campaign_space
signature:
docs: docs/re/civ.md#civ-stage-end
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
CORRECTION (2026-11-08, fix/w46-g4-tests): the first root cause below ("the last city went to the players through the
surrender talk") is wrong: in lab 20261030-104149 campaign_space#1 the 10th city was the shortcut's own cCity::Capture
(test log 11:00:18 "Capture of 1 city (1 of other nations listed)", host log 12:00:15 "Alice captured a city", the
cinematic, owned_cities=10 at 12:00:52; the host clock runs 1 h ahead). The talk at 11:00:37 moved no city.
Observed (Ghidra static, docs/re/civ.md#civ-stage-end): the end of Civ is not checked periodically; `GameCiv_IsPlanetOwned`
(player civ level +0x298 > 3, raised by AddCity when the civ owns every city of the planet) runs only (a) inside
Capture's instant branch (arg3 != 0: what a nation's surrender uses), (b) at the end of the CVG_Capture cinematic,
unless the city's capture type differs from its specialty: then the game shows the specialty box and pauses, and (c)
when that box is answered for exactly the last-captured city (player civ +0x44C). The shortcut captured by the
cinematic path (instant 0). Inferred from the two failing runs (20261030-104149 #1, 20261103-012624 #3): the last
city's box was not answered (the bot's "dialog Military" press answered the previous city's box, or a talk / the Esc
pause menu was in the way: the 012624 screenshot shows the Options menu open), so the check never ran again.
Recurrence 2026-11-01..08: 26 campaign_space runs; the last city by a talk in 16 (all passed), by the shortcut's
cinematic capture in 10 (9 passed after the box was answered, 1 failed: 20261103-012624 #3). A game rule the test hit,
not a co-op bug.

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
Test only (tools/tests/coop_campaign.py kick_civ): the shortcut takes the last city of another nation with Capture's
instant flag (`Capture(newPol, 0, 1, 0, 0)`), as a surrender does: ChangeOwner and the end-of-Civ check run inside the
call, no caption, no specialty box. The other cities still go by the cinematic path.

## Verification
FAIL (before, the cinematic path for the last city): lab 20261030-104149 campaign_space#1, SporeCoop out/lab/20261103-012624 #3.
PASS: out/lab/20261108-081625 #1 (lane 3): the last city taken instantly ("Capture of 1 city ... (1 of other nations
listed)" 08:25:08, no specialty box), the players' vote 53 s later, the editors, Space after 408 s; out/lab/20261108-072213
(the last city by a talk, the end as before). no-fail-run: the failing timing (a box left unanswered) is not reproducible
on demand; the instant path takes the box out of the shortcut's way, as a surrender does.

## History
- 2026-10-30: opened.
- 2026-11-08 (fix/w46-g4-tests): G4: the cause (docs/re/civ.md#civ-stage-end), a CORRECTION of the first root cause, the shortcut's last capture instant; campaign_space PASS with the instant path (out/lab/20261108-081625 #1); fixed.
