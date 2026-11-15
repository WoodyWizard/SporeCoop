---
id: BUG-0199
title: Not covered by tests yet
status: fixed
kind: test
stage: All
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: 
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.11
branch: fix/w46-g4b-coverage
commits: 3f55f0d2, bcbe471a
tests: tribe_healer, space_store, civ_religious, civ_roads, party_civ_trade, space_armada_return, space_contracts
signature:
docs: docs/06-testing.md#testing-g4b-coverage, docs/re/tribe.md#tribe-healer-runtime, docs/re/space.md#space-npc-store-runtime
related: BUG-0181, BUG-0189, BUG-0745, BUG-0747, BUG-0748, BUG-0749, BUG-0750, BUG-0751
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* Tribe: duet and healer bonus in real play (play_tribe.py plays real raids since 2026-10-23:
  pincer, the chief's aura and rescues happen there; the fixture's tribe has no instruments and
  no healer tool, so a social click on another tribe's hut is a food gift, CitizenAction 9, and
  nobody can heal: a fixture of a later tribe save is needed). Civilization: the religious role
  and trade in real play (play_civ.py plays military vehicles: the military role and joint sieges
  by real vehicles are covered). Space: raids and missions (no live mission in
  the fixture), spice trading with NPC stores (selling to a partner colony is covered by coop_space_trade.py), terraforming with the tools, buying in the city planner (the bot
  opens and closes it only).

## Root cause
An umbrella of test gaps. State on 2026-11-08 (fix/w46-g4b-coverage), item by item:
* Civ religious attack: covered by `civ_religious`. Civ trade routes: `civ_roads`, `party_civ_trade`. Space armada
  raids: `space_armada_return`, `space_contracts` (armada). Buying in the Civ city planner: BUG-0189, `civ_planner_build`.
  A save after the Space tutorial: BUG-0181, fixture `space_free`.
* Tribe healer bonus: new test `tribe_healer`; it found the bonus never fired (BUG-0745, fixed in the same branch).
* Space spice sold to an NPC store: new test `space_store` (comm screen clicks); it found that each game has its own
  store (BUG-0747, open).
* Tribe duet: BUG-0748. Space terraforming with a tool: BUG-0749. Space NPC missions and their reward: BUG-0750. A guest's
  building in a Space colony planner: BUG-0751. Each child has the recipe, the attempt run of 2026-11-08 and what
  blocked it.

## Fix
`coop_tribe_healer.py` (suite `tribe_healer`) and `coop_space_store.py` (suite `space_store`), the switches
`tribe.heal_max` (BUG-0745) and `res.guest_send` (the twin of a guest's earning), the test command
`coop.space allies missions [done <ptr>]`; the open items are the concrete child tickets above.

## Verification
* tribe_healer: `SPORE_FIX_OFF=tribe.heal_max` FAIL lab 20261108-063242; PASS 3 of 3 lab 20261108-063444 (lane 3).
* space_store: `SPORE_FIX_OFF=res.guest_send` FAIL lab 20261108-064548 (the guest's sale only); PASS 3 of 3 lab
  20261108-065802 (lane 3; an earlier 3-run batch 20261108-064803 had one run stopped by the sync-check gate on the
  NPC empire's ship whose model the guest lacks, now allowed with BUG-0424).

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests: not reached: every item needs a later fixture (a tribe with instruments and a healer tool, a live Space mission, raids, a civ with designs: BUG-0189, BUG-0181). Stays open.
- 2026-11-08 (fix/w46-g4b-coverage): tests tribe_healer (found BUG-0745) and space_store (found BUG-0747); the rest split into BUG-0748..0751 after attempts with runs; closed as an umbrella.
- 2026-11-08: released in 0.7.11.
