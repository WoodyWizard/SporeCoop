---
id: BUG-9279
title: Galaxy planet: a guest that takes the planet over in place gives its own party's bar and relations to the old host's party
status: fixed
kind: bug
stage: creature
reported: 2026-11-19
source: TITAN test 2026-11-19 (BUG-9278)
role: host
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits:
tests: tools/qa_win (live TITAN QA 8+9 2026-11-19, the host moves on, the guest takes over in place)
signature:
docs: docs/12-server-and-players.md#server-party-stage-end
related: BUG-9270, BUG-9277, BUG-9278
---

## Report
Seen in BUG-9278's test (TITAN, 2026-11-19): QA 9 (a guest of another party) took Samuth (Creature) over in place when
QA 8's party moved on, and logged "this planet's bar was another party's (60): this host's party plays its own (60)".
The 60 was QA 9's own party's bar: it was saved as QA 8's party's, and QA 8 later loaded the planet with 60 (its own
was 1078). The relations did the same ("the old host's party keeps the game's").

## Root cause
BUG-9270 and BUG-9277 take the game's own relations table and bar as the old host's party's when a host of another
party has the planet. That holds for a world loaded from the stored save, not for a guest promoted in place (6B C):
its game's table and bar were already its own party's (MSG_SPECIES per party, MSG_DNA per party).

## Fix
A guest notes the world in which it applied its party's table and bar; hosting that same world (promoted in place),
the swap is skipped: the game's table and bar stay its party's, the old host's party keeps what the file holds. Switches
`social.party_relations_warm` and `creature.party_bar_warm`.

## Verification
FAIL (before the fix, TITAN 2026-11-19 21:06): QA 9 promoted in place gave its bar (60) to QA 8's party; QA 8 then
loaded the planet with 60 (`coop.dna bar`: "bar=60 ... swaps 1").
PASS (21:55): QA 9 promoted in place logged "its own party's bar stays (60) (BUG-9279)" and "the game's relations stay
its own party's (BUG-9279)".

## History
- 2026-11-19: found in BUG-9278's test and fixed on feat/lineages-alpha.
