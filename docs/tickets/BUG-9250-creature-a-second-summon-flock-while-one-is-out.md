---
id: BUG-9250
title: Creature: a second Summon Flock while one is out does nothing and spends the ability; lock it per party with a message
status: fixed
kind: bug
stage: Creature
reported: 2026-11-17
source: owner 2026-11-17 (decision 5FA)
role: host
versions: 0.7.14.1-lineages63-dev
fixed_in: 0.7.14.1-lineages64-dev
branch: feat/lineages-47
commits: f867734e
tests: manual Windows QA with tools/qa_win/qa.py (TITAN host 9 + partner 8, the MSI as player 2; Samuth.spo), `coop.gpack baruse 33`
signature:
docs: docs/re/creature.md#creature-summon-lock
related: BUG-9245, BUG-9248
---

## Report
Owner 2026-11-17 (decision 5FA): "let's lock summon flock when one is active in the party. This will allow for party
tuning later on when everyone can use powers at the same time." Before: while one player's Summon Flock was out (about
2 minutes), another player's press did nothing at all and nobody said why.

## Root cause
Spore's world has one summon event (docs/re/creature.md#creature-pack-per-player): a second start finds "none idle".

## Fix
The press is refused before the game uses the ability (AbilityBar_Use, the one path of the keys and the bar's buttons)
while the party has its limit of flocks out, with a line in that player's feed ("Summon Flock: Titan's flock is out. One
at a time in the party." / "Your flock is out."). The host counts the flocks out (its own and the partners') and tells
the guests (K_SUMMONLOCK 17, 1 Hz and at a change); it also refuses a guest's summon request while locked. The limit is
the party's (`coop.gpack summonlimit <n>` on the host, 1 by default); the game runs one at a time, so a limit over 1
changes only the line ("Spore runs one flock at a time in a world") until each player's summon is its own (5D / 5FB).
Switch: `coop.fix creature.summon_lock off` (no lock, 63-dev).

## Verification
PASS (08:39-08:42): player 1's flock out (`coop.gpack summon 1`): host, partner and MSI all locked=1 owner=1; the
host's, the MSI's and the partner's own Summon Flock presses (`coop.gpack baruse 33`) refused (blocked=1 on each), the
feed lines as above on the host and the partner; with the limit 8 the MSI's line "Spore runs one flock at a time in a
world"; the flock ended 2 min 15 s later: all three unlocked, the MSI's press went through again.
FAIL (switch off on the host, 08:41): the host's press went through to the game while the flock was out (blocked stayed 2).
Not covered: Summon Flock is not unlocked in the QA world (status 10), so no press here could start a flock; the host's
refusal of a guest's request was not reached end to end.
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-17: opened and built (decision 5FA), tested on TITAN and with the MSI.
- 2026-11-17: released in 0.7.14.1-lineages64-dev.
