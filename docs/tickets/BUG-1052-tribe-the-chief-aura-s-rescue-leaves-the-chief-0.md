---
id: BUG-1052
title: Tribe: the chief aura's rescue leaves the chief 0.3 hit points instead of 30 % (maximum read from +0x5D4)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-13
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-small
commits: f4aca716
tests: tribe_chief_rescue
signature:
docs: docs/known-issues.md#issue-tribe-chief-rescue-hp, docs/06-testing.md#testing-t1-small
related: BUG-0745
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-13. Not seen at runtime, found by code reading.
With the session setting tribe.chief_aura on (the default), a chief about to fall next to a partner's member is
"saved" (notice "<A> saved <B>'s chief"): the blow should leave it 30 % of its maximum health. The maximum is read
from the citizen's +0x5D4, which holds 1.0 (BUG-0745), so the chief keeps 0.3 hit points and dies at the next blow,
and the rescue's 60 s guard then blocks a second save.

## Root cause
Confirmed at runtime: the rescue's maximum came from the citizen's +0x5D4 (1.0, BUG-0745) in h_CitizenTD; a 168 hp
chief kept 0.30 after a blow of 173 (lab FAIL run below), on the host and on the guest's mirror.

## Fix idea
Use game::max_health(victim) (cCombatant::GetMaxHitPoints) as the heal hook does; switch idea
`coop.fix tribe.rescue_max off` (or reuse tribe.heal_max).

## How to test
New or extended tribe test (e.g. tools/tests/coop_tribe_chief_rescue.py, tribe fixture): a partner's member stands
next to the host's chief, a test command deals a blow larger than the chief's health through the game's TakeDamage
path; assert the chief's health afterwards (the game's +0x5E0) is about 30 % of game::max_health. Main: 0.3: FAIL.

## Fix
h_CitizenTD takes the maximum from game::max_health (cCombatant::GetMaxHitPoints), as the heal hook of BUG-0745.
Switch `coop.fix tribe.rescue_max off`: the field +0x5D4 (0.7.13).

## Verification
Test `tribe_chief_rescue` (coop_tribe_chief_rescue.py: the guest's chief put next to the host's, a blow 5 over the
host chief's health through TakeDamage; its health must be 30 % of its maximum on the host and on the guest's mirror).
FAIL (SPORE_FIX_OFF=tribe.rescue_max): out/lab/20261113-031145 (168 -> 0.30 on both games). PASS: out/lab/20261113-030613, out/lab/20261113-031904
(168 -> 50.4 on both games).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-small (package T1).
- 2026-11-14: released in 0.7.14.
