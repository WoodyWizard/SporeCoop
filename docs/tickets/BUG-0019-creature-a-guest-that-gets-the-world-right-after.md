---
id: BUG-0019
title: Creature: a guest that gets the world right after the host's editor stays a hatchling
status: fixed
kind: bug
stage: Creature
reported: 2026-10-25
source: test (coop_migration)
role: guest
versions: 0.7.2.4
fixed_in: 0.7.3.0
branch: fix/w3-creature
commits: 4096536
tests: evolve_world, migration
signature: 
docs: docs/known-issues.md, docs/re/creature.md#creature-world-epoch
related:
---

## Report
The world sent right after an evolution that starts a migration is saved during CRG_SpeciesMigrate; the guest's creature stays at age 0 and never gets the Migrate goal.

## Root cause
Observed (lane 5, 2026-10-28): the host's share after an evolution (`coop.cpp follow_phase`,
`needs_world` set by `on_mode_change`) went out as soon as the host was back in the stage, while
its game played CRG_SpeciesMigrate with the caption waiting for OK. The save made then has the
host's creature at age 0 (+0xB34) and the migration property 0x514A219 at 1; the game sets age 1
and state 2 only when the caption is confirmed. The guest's game that loaded that world kept age 0
and state 1 (never the goal "Migrate to a new nest").

## Fix
The share of a world after a mode change waits 3 s when the host evolved (the cinematic starts
after the editor) and while any cinematic plays, at most 120 s (`coop.cpp follow_phase`).
`coop.npc sharecine off` keeps the old behaviour (tests).

## Verification
Test `tools/tests/coop_evolve_world.py` (suite `evolve_world`): DNA for a bigger brain, the
evolution vote, both leave their editors, the host reads the caption for 20 s and confirms it;
checked on the guest's game after its load: the avatar's age (+0xB34) and the property 0x514A219.

* FAIL without the fix (`--without-fix` = `coop.npc sharecine off` on the host, lane 5, 2026-10-28,
  direct run): the guest loaded twice while the caption was up and ended with age 0, migration 1
  (the host: age 1, migration 2).
* PASS with the fix (lane 5, direct run; lab run out/lab/20261028-010504, `evolve_world`): no load while the caption was up; the guest has age 1,
  migration 2.

## History
- 2026-10-25: reported (test (coop_migration)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: root cause found and fixed on fix/w3-creature (the share waits for the cinematic); test evolve_world (FAIL with sharecine off, PASS with it).
- 2026-10-28: released in 0.7.3.0.
