---
id: BUG-9254
title: Galaxy planet: one player's migration opens the creature editor of every player on the planet, and the host's editor holds joins
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: qa
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 92cc35fe
tests: coop.migratetest (manual: TITAN QA 8 + 9 and the MSI on one galaxy planet; no lab test yet)
signature: "'s species is migrating to a new nest. Everyone migrates together" on players of other parties
docs: docs/12-server-and-players.md#server-migrate-party
related: BUG-9163
---

## Report
Seen in the 2026-11-18 alpha tests (owner decision 6R A asked for the fix): when a player entered a galaxy planet,
the host and the other guests were sent into the creature editor and had to press the check; the joiner then waited
at "Joining" because the host's editor holds the world upload ("no save for the upload now: an editor is open").

## Root cause
Observed (host log of 18:41, previous/sporekit.9-20261118-191519.log): "QA8's species is migrating to a new nest.
Everyone migrates together: your editor opens in 10 s for final touches." BUG-9163 (decision 2G, from the 2-4
player co-op) makes a migration by any player open everyone's editor: the host passes a guest's MSG_MIGRATE to every
guest and every game opens its editor. On a galaxy planet the players are strangers in different parties (or none).

## Fix
coop.cpp `migrates_with`: on a galaxy planet a migration warning counts only when the migrating player is in this
game's party (`parties::same_party`; each game knows its own party, so the host still passes every warning on and each
game checks it). A game whose own migration has no party tells nobody. A direct session (no galaxy) keeps "everyone".
Switch: `coop.fix creature.migrate_party off`. Test command `coop.migratetest` sends the migration message as a real
evolution does.

Not fixed here: a host in its own (chosen) editor still holds a joiner's world until it is back in the stage.

## Verification
no-fail-run: no lab test (galaxy planets have none yet); manual runs, 2026-11-18, MSI host of Samuth, TITAN QA 9 and
QA 8 guests, QA 8 `coop.migratetest`:
- fix on, no party: QA 9 and the MSI stay in Creature; QA 9 logs "not this game's party's: no editor here".
- fix off on QA 9 and the MSI (FAIL): both in the Editor 10 s later ("Everyone migrates together").
- fix on, QA 8 and QA 9 in a party: QA 9 in the Editor ("Your party migrates together"), the MSI stays in Creature.

## History
- 2026-11-18: opened and fixed on feat/lineages-alpha.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
