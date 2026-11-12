---
id: BUG-0940
title: Civ: a guest's vehicle purchase with the last shared 1000 made no vehicle on the host and no refusal was counted (campaign, four players buying at once)
status: fixed
kind: bug
stage: Civilization
reported: 2026-11-11
source: final check 2 of 0.7.12, campaign (lab 20261111-090547), play_civ PROBLEM line
role: guest
versions: 
fixed_in: unreleased
branch: fix/w48-rest
commits: 3204a8d8
tests: civ_buy_race, play_civ, campaign
signature:
docs: docs/06-testing.md#testing-civ-buy-race
related: BUG-0613
---

## Report
rel/0.7.12 lab 20261111-090547, campaign (four players), Civ play, 09:56: `PROBLEM Bob bought a vehicle in city 0
(button 062D6902) and the host made none (money 1000.0 -> 0.0)` (play_civ.py `buy`: no vehicle of Bob's on the host
6 s after the click, the city still Bob's or shared, the host's `buys_refused` not raised). A PROBLEM line of the
bot, not a finding: the campaign did not fail on it. In no other campaign log on this machine.

## Root cause
Not established. Observed: the four bots bought at once: the shared money went 3000 -> 2000 -> 0 between 09:56:25 and
09:56:39 (three purchases of 1000 made on the host), the host's `rejected=0` and `buys_refused=0`, and Bob's game
counted `cmds_sent=0` at 09:56:32. The host's money was 1000 when the bot read it and 0 after its wait, so another
player's purchase took the last 1000 around Bob's click. Inferred: Bob's own game refused the click (its shared money
was 0 by then: the game's own "not enough money"), nothing was sent, nothing was lost: a race of the bots that the
check does not know (it treats only "the city changed hands" and "refused by the host" as no purchase).
Not related to BUG-0933 (Space money held on a guest): Civ money was in sync on all four games.

Seen again in fix/w47-final12b lab 20261111-113158 (campaign): `PROBLEM Alice bought a vehicle in city 0 (button
062D6902) and the host made none (money 1000.0 -> 0.0)`: Alice is the host itself, so no message was involved: the
bots' race on the last 1000, a problem of the bot's check.

Proven (2026-11-12, fix/w48-rest, lab lanes 4 / 5): the race made on purpose (play_civ's own Bot on the guest, the
host spending the whole shared money between the bot's look at its enabled purchase button and its click): no
vehicle of the guest's on the host, the money 0 on both (never below), the guest's game sent no purchase
(`cmds_sent` unchanged: its own game refused the click, the mod's h_UIMessage never got an activated button), and
play_civ said "PROBLEM Bob bought a vehicle in city 0 (button 062D6902) and the host made none (money 20000.0 -> 0.0)",
the campaign's line. No game or mod bug: a check of the bot that did not know this outcome.

## Fix
tools/tests/play_civ.py `a_buy`: the guest's `cmds_sent` is read right before the click; when no vehicle came, the
city is still the player's, the host refused nothing, the shared money fell below what it was before the click and no
purchase was sent (a host's own click: no command), the bot counts `buy_broke` ("the shared money ran out before the
click") instead of a PROBLEM. A purchase sent and lost stays a PROBLEM. Test-only (no switch).

## Verification
Test `civ_buy_race` (tools/tests/coop_civ_buy_race.py, civ fixture): a control purchase with money, then twice the
race; on the games' own state: no vehicle, money 0 on both, nothing sent; and the bot's verdict.
FAIL (play_civ before the fix, 6a50d72a): out/lab/20261112-134053 (both races "lost", the PROBLEM line).
PASS: out/lab/20261112-134351, PASS2_0940.

## History
- 2026-11-11: opened.
- 2026-11-12: the race made on purpose (civ_buy_race): the game refuses the click itself, nothing is sent or lost; play_civ counts it as buy_broke (fix/w48-rest).
