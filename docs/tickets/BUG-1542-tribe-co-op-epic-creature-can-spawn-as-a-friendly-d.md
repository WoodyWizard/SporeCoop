---
id: BUG-1542
title: Tribe: co-op epic creature can spawn as a friendly/domesticated species near the village
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-24
source: Discord bug report 2026-11-24 (SporeCoop-report-20261124-214704)
role: both
versions: 0.7.16, 0.7.16.1, 0.7.99
fixed_in: 0.8.0
branch: w08/tribe-fixes
commits: 4809df7d
tests: tribe_epic_hostile, tribe_epic_hostile_off
switch: tribe.epic_hostile
signature:
docs: docs/re/tribe.md#tribe-epic-hostile-and-ufo
related: BUG-0121, BUG-0146, BUG-0515, BUG-0563
---

## Report
During Tribe stage, when the co-op epic encounter triggers ("an epic creature comes! together: 80 food"), the spawned
epic creature can be friendly to the player tribe instead of hostile.

## Root cause
In `mods/coop/tribe.cpp` (`spawn_epic`), the species profile for the epic was chosen as the `cCreatureAnimal` closest to
the player village (`d < pk->bd`). In Tribe stage, domesticated pets in the village pen (`cTribe+0x19CC`
`mpDomesticatedAnimalsHerd`, `herd+0x88 == 0xD77DC235`, `herd+0x15C == 7`, `animal+0xB65 == 1`, `animal+0x30 == tribe+0x30`)
or allied/friendly species near the village are closest to the village. `cSpeciesRelationshipManager::GetRelationship`
(`0x00BA40E0`) checks the species map (`mgr+0x20`) before the creature's archetype tuning (`+0x390`), so an epic spawned
with a domesticated/allied (`6`) or friendly (`4`) species kept that friendly relationship and `cCreatureBase::IsHostileTo`
(`0x00C0B9C0`) returned false.

## Fix
Behind `COOP_FIX("tribe.epic_hostile", "BUG-1542", ...)`:
- Added tiered candidate selection (`tribe_logic::epic_candidate_tier`) in `mods/coop/tribe.cpp` (`spawn_epic`) to skip
  domesticated pets (and wild animals of any domesticated species) as well as friendly/allied species (`rel == 4 || 6`,
  `herd+0x190`/`+0x191`), preferring wild undiscovered/hostile species and falling back safely only if none exist.
- Cleared domesticated/friendly flags on the spawned epic herd (`+0x84`, `+0x15C`, `+0x190`, `+0x191`) and creature
  (`+0xB65`, player political ID `+0x30`), ensured non-friendly wild species picks have a hostile species relation (`5`),
  and hooked `Simulator::cSpeciesRelationshipManager::GetRelationship` (`0x00BA40E0`) so relation queries involving the
  live co-op epic (`g_epic`) in Tribe stage always return `5` (hostile).
- Added native unit tests in `tests/tribe_tests.cpp` and Wine lab test `tools/tests/coop_tribe_epic_hostile.py` with
  OFF-twin `tribe_epic_hostile_off`.

## Verification
- PASS (`tribe_epic_hostile`): `out/lab/20261125-025515` (lane 3; domesticated `06808ACA` at 5.0 m from village with
  `dom=1 rel=6`; epic skipped 46 domesticated/friendly candidates, picked wild `0654E5CB`, `rel=5 hostile=1`, landed
  67.5 damage blow on player tribe member; 0 host faults, 0 guest faults).
- FAIL (`tribe_epic_hostile_off`, `SPORE_FIX_OFF=tribe.epic_hostile`): `out/lab/20261125-025515` (lane 3; epic picked
  domesticated pet species `06808ACA` with `rel=6 hostile=0`).

## History
- 2026-11-24: opened from Discord bug report.
- 2026-11-25: fixed in `4809df7d` on `w08/tribe-fixes`, verified PASS + OFF FAIL in `20261125-025515`.
- 2026-11-25: released in 0.8.0.
