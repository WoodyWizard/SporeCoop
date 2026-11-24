---
id: BUG-9362
title: One Summon Flock per world: a second party's summon does nothing while another's flock is out
status: fixed
kind: bug
stage: creature
reported: 2026-11-23
source: owner
role: 
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 97fdf70d
tests: tools/qa_win (manual, the MSI: coop.prules roar 33 on the host, then on the guest; coop.gpack, coop.drama summon)
signature:
docs: docs/re/creature.md#creature-summon-per-party
related: BUG-9250, BUG-9245, BUG-9233, BUG-0736
---

## Report
Owner 2026-11-23 (5FB "it should be all abilities working", 8RB: one Summon Flock per party). Two parties on one
planet (one session, one host game): while one party's flock was out, another party's Summon Flock did nothing
("summon event for a partner's creature: none idle"); with the party lock (BUG-9250) counting the world's flock, it
was even refused ("the party's flock is out").

## Root cause
Spore's world has one cSummonCreatureEvent (the drama manager makes it once, from its factory table); StartKind(8)
starts only an idle event. The mod's lock counted that one event for everybody.

## Fix
drama.cpp summon_extra / flock_tick: a party whose flock is not out while the world's event runs gets an extra event
of its own (the game's allocation and constructor SporeCoop::SummonEvent_ctor, the world event's settings and property
list, StartEvent's steps, the game's own start, update, done? and end), led by the summoner (the host's avatar, or the
partner's puppet inside its pack, as the world event for a partner). The lock counts the flocks of the summoner's
party (parties::same_party), each guest hears its own party's count. Never in the manager's list: never saved. Switch
`coop.fix creature.flock_per_party off`: one per world, the lock world-wide, as before.

## Verification
The MSI (TITAN's galaxy server; the host MSIHost-NoShare- and the guest MSIGuest-NoShare- in two parties, Samuth's
Creature era):
- FAIL (switch off): the host's Summon Flock (the world event, 111 s), then the guest's: "player 1's Summon Flock
  refused: the party's flock is out (player 0, 5FA)", the guest's pack 0.
- PASS (switch on): the host's flock out, then the guest's: "an extra Summon Flock (event ...) for player 1, 120 s",
  "the flock summoned: 3 in player 1's own pack"; the guest's own game showed them in its Pack panel (mirror=3,
  added=3) and beside its creature, the host its own flock (images/bug9362_two_flocks.png); the extra event ended by
  itself 126 s later, both games running, no crash.
- Found on the way: the first version took the event's +0x44 for a pointer (it is the key 0x614A7EA) and the host's
  game faulted twice (crash-20261123-235204-i2, -235722-i2): fixed before the PASS run.
no-fail-run: the FAIL is the switch off on the same build.

## History
- 2026-11-23: opened.
- 2026-11-24: fixed on feat/lineages-alpha (97fdf70d), FAIL and PASS seen on the MSI.
