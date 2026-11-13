---
id: BUG-1042
title: Space: a partner's terraforming record (K_TERRA_REC) calls GetPlanetRecord unchecked: a star without generated planets, an id out of the star table, or a game outside Space can fault
status: open
kind: crash
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-2
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-terra-rec-unchecked
related: BUG-0883, BUG-0284
---

## Report
Audit SPC-2 (handed over 2026-11-12 for 0.7.14). Not seen at runtime, found by code reading.
When a partner terraforms, its game sends the planet record values (K_TERRA_REC, BUG-0883) to everyone. A receiver that is not on that planet writes them into `cStarManager::GetPlanetRecord(id)`. What could happen: the receiver's game faults (a crash, or a caught fault every second while the partner terraforms) when that star's planets were never generated in its game (a star far from where it flies), when it is not in Space (loading another world, the galaxy menu), or when a peer sends a bad planet id.

## Root cause
Observed in code (main efd2e0ba):
* mods/coop/spacecoop.cpp:1542 `on_message` gates MSG_SPACE on `g_ok` only; 1636 K_TERRA_REC -> `apply_terra_rec`.
* spacecoop.cpp:1146-1171 `apply_terra_rec` checks `g_terra_elsewhere_fix`, the planet bit 0x40000000 and `a_StarMgr`, then (1159-1161) calls `cStarManager::GetPlanetRecord(planet_place & 0x3FFFFFFF)` directly; `IsBadWritePtr(rec, 0xBC)` (1163) runs only after the call. No `in_space()` (unlike apply_terra's own-planet path and every sender), no check that the star exists in the table, no check that its planets were generated.
* docs/re/space.md (the BUG-0284 notes near "A planet id is planet index << 24 | star index"): GetPlanetRecord 0xBA6F10 indexes the star table +0xC8 with no bounds or "has planets" check, and cStarRecord::GetPlanetRecord 0xBBABD0 reads `records[i]` of a star whose count is 0 as a null read at 0xBBABE2 (that was BUG-0284's crash).
* The mod already has a bounded lookup, `spacecoop::star_record` (spacecoop.cpp:526), and space_allies.cpp:159 checks the star's +0xAC (planets generated) before RequirePlanetsForStar.
Inferred: whether a star of the shared galaxy can be without generated planets in the receiver's game while the partner stands on one of its planets (likely for distant stars; BUG-0883's lab saw an unvisited planet with a record, so generation reaches further than visits). Confidence: medium for a player crash, high for the missing checks.

## Fix idea
In `apply_terra_rec`: return unless `in_space()`; look the star up with `star_record(place & 0xFFFFFF)` (bounded); skip it unless its planets are generated (+0xAC, as space_allies.cpp) and the planet index is under the star's record count; only then call GetPlanetRecord. Optionally keep the last value per planet and apply it when this game generates that star. Switch: `coop.fix space.terra_rec_checked off` (the call as before).

## How to test
New `coop_space_terra_far.py` (space fixture, 2 players): the host terraforms a planet of a star the guest's game has not generated (a test command on the host that sends K_TERRA_REC for a far star id and one for an id past the star table, and the same while the guest is at the galaxy menu / loading). Assert the guest's game has no fault or crash report and its record of a generated planet still changes. Main: expect a fault report (FAIL) on the far / bad id; with the fix PASS. Existing `coop_space_terra_tool.py` must still pass.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
