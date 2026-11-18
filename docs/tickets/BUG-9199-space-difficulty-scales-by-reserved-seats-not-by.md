---
id: BUG-9199
title: Space difficulty scales by reserved seats, not by the party in the system (decision 2B)
status: fixed
kind: feature
stage: space
reported: 2026-11-14
source: roadmap
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 160d5865
tests: tools/qa_win (two TITAN games by hand: Grox ships spawned in the host system, the guest lands)
signature:
docs: docs/re/space.md#space-party-difficulty
related: BUG-9100 BUG-9197
---

## Report
Roadmap decision 2B A (the owner, 2026-11-12: "Difficulty will need to be per party"): Space enemy health, enemy damage and the extra raid ships scaled with the campaign's reserved seats. A player alone in a system of a four-seat campaign met ships made for four.

## Root cause
Observed (code and the FAIL run): squadron.cpp scale_local_ships and h_TakeDamage, and space_allies.cpp h_BuildRaidFleet, used `world::rules_for_current()` whose `seats` is the reserved seat count.

## Fix
space.party_difficulty (on): `squadron::party_rules(here)` is the campaign rules with `seats` = the players in this game's place (PlayerState.world in Space, the place of the fleet leader, BUG-9197) for ship health and damage, the players online for a raid on the party's colony; outside a session 1. With one party per session (today) that is the party in that system. A ship already scaled is rescaled when players come or go (its health fraction kept). Status: `coop.fleet list` line "space difficulty". Tests: `coop.fleet difficulty <percent>` (memory only), `coop.fleet spawn <type> pol <hex>`, `coop.space allies enemies`.

## Verification
Live, two TITAN QA games (2026-11-14, host DuoQAJiffSpace in its solar system, a four-seat campaign, test difficulty 100 %, war on the Grox, two Grox ships spawned next to the host): PASS (on): with the guest in the system the ships had 460/460 and 2300/2300 (115 %, two players); after the guest landed on the planet (another place) 400/400 and 2000/2000 (the game's own), raid extra 1 (two online). FAIL (`coop.fix space.party_difficulty off`): 580/580 and 2900/2900 (145 %, four reserved seats) both times, raid extra 3. no-fail-run: not a lab run (TITAN QA games, by hand).

## History
- 2026-11-14: opened.
- 2026-11-14: fixed (the party in the place), live-tested with two games.
