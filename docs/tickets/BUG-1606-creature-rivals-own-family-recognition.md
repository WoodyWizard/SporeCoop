---
id: BUG-1606
title: Creature Rivals: guest own-species nestmates treated as neutral wild NPCs for quests and social/hunt actions
status: fixed
kind: bug
stage: Creature
reported: 2026-11-27
source: AI playtest (v0.8.2.3, PLAYTEST-045) and Discord report (SporeCoop-report-20261127-200429.zip)
role: guest
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 3ccb71f1
tests: rivals_creature, make test
signature: Creature Rivals guest receives Hunt/Impress wild-species quest targeting own species
switch: none (own-species relationship and social-mode guards in creature_rivals.cpp)
docs:
related:
---

## Report
Both the 6.5-hour 4-player playtest (`PLAYTEST-045`: guest got `"Hunt 6 Cadian"` targeting its own species) and Smile's Discord report (`SporeCoop-report-20261127-200429.zip`: guest got `"Impress 3 2345"` targeting its own species `2345`) showed Creature Rivals guests treating their own nestmates/species as neutral wild NPCs.

## Root cause
1. In `creature_rivals.cpp` `h_SocialMode`, clicking an own-family creature (`owner == me`) fell through to native social-mode evaluation instead of returning `0`.
2. In `override_relationship`, the `pa == pb` own-family Allied (`6`) return was gated behind `!substituted`, so 1-arg rollover and quest queries (`b == nullptr`, where `b` is substituted with the local player) missed the Allied check and returned Neutral/wild relationship for the guest's own species.

## Fix
- Returned `0` in `h_SocialMode` when `owner == me` so own family in Creature Rivals never starts a wild-species Impress/Hunt session.
- Returned `rel_out = 6` (Allied) in `override_relationship` whenever `pa != 255 && pb != 255 && pa == pb`, including on 1-arg `substituted` rollover and quest queries.

## Verification
- PASS run: `out/lab/20261127-145438` (`rivals_creature`, PASS, 43 s, 0 faults) and full `make test`.
- no-fail-run: reproduced in `out/ai-playtest/20261127-075046` (`PLAYTEST-045`) and Discord report `SporeCoop-report-20261127-200429.zip`.
