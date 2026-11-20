---
id: BUG-9281
title: Galaxy planet: the Tribe food share and its panel run between different parties
status: fixed
kind: bug
stage: tribe
reported: 2026-11-19
source: TITAN test 2026-11-19 (BUG-9278)
role: all
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits:
tests: tools/qa_win (live TITAN QA 8+9 2026-11-19, two parties in Samuth (Tribe))
signature:
docs: docs/12-server-and-players.md#server-party-stage-end
related:
---

## Report
TITAN 2026-11-19 22:47 (images/stage4_join_tribe_guest_view.png): QA 8 and QA 9, two different parties on
Samuth (Tribe), both show the panel "Village food Host 49.55 Guest/You 15.00": the co-op food share (decision 2P) runs
between strangers.

## Root cause
Inferred: the Tribe food share and its panel count every founder seat of the session; on a galaxy planet they
should count the own party's seats only (as difficulty and relations do since BUG-9266 and BUG-9267).

## Fix
tribe.cpp food_partners (switch `tribe.food_party`): on a galaxy planet the host shares a delivery only with the
gatherer's own party's villages (parties::table_key), refuses a party gift to another party's village ("That village
is not your party's."), and the HUD line lists the host's party's villages only; a guest whose host is another party's
shows no line.

## Verification
FAIL (22:47, before the fix): both games showed "Village food Host 49.55 Guest/You 15.00"
(images/stage4_join_tribe_guest_view.png). PASS (23:17, the same two parties in Samuth (Tribe)): no food line on either
game (images/stage4_bug9281_pass_guest.png). Not seen: a delivery shared within a party of two on a galaxy planet.

## History
- 2026-11-19: opened and fixed on feat/lineages-alpha.
