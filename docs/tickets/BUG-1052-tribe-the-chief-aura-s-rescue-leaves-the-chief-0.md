---
id: BUG-1052
title: Tribe: the chief aura's rescue leaves the chief 0.3 hit points instead of 30 % (maximum read from +0x5D4)
status: open
kind: bug
stage: Tribe
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-13
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-tribe-chief-rescue-hp
related: BUG-0745
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-13. Not seen at runtime, found by code reading.
With the session setting tribe.chief_aura on (the default), a chief about to fall next to a partner's member is
"saved" (notice "<A> saved <B>'s chief"): the blow should leave it 30 % of its maximum health. The maximum is read
from the citizen's +0x5D4, which holds 1.0 (BUG-0745), so the chief keeps 0.3 hit points and dies at the next blow,
and the rescue's 60 s guard then blocks a second save.

## Root cause
Observed in code (main efd2e0ba), confidence high (the field's value is BUG-0745's runtime finding):
mods/coop/tribe.cpp:4264 in h_CitizenTD: `float hp = F<float>(victim, 0x5E0), mx = F<float>(victim, 0x5D4);` then
tribe.cpp:4270 `dmg = std::max(0.f, hp - mx * 0.3f);`. BUG-0745 moved the heal hook (tribe.cpp:4310) to
game::max_health behind `tribe.heal_max`, but missed this second read of the same field.

## Fix idea
Use game::max_health(victim) (cCombatant::GetMaxHitPoints) as the heal hook does; switch idea
`coop.fix tribe.rescue_max off` (or reuse tribe.heal_max).

## How to test
New or extended tribe test (e.g. tools/tests/coop_tribe_chief_rescue.py, tribe fixture): a partner's member stands
next to the host's chief, a test command deals a blow larger than the chief's health through the game's TakeDamage
path; assert the chief's health afterwards (the game's +0x5E0) is about 30 % of game::max_health. Main: 0.3: FAIL.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
