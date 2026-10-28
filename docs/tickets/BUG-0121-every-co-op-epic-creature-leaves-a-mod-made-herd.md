---
id: BUG-0121
title: every co-op epic creature leaves a mod-made herd in the world, and a live epic can go into the host's save
status: fixed
kind: bug
stage: Tribe
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#100
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-civ
commits: 421bfe0
tests: tribe_epic
signature:
docs: docs/re/tribe.md#tribe-epic-lifetime
related: #100
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/100 (full text, discussion).

In the Tribe stage the host spawns an epic creature for the players every 10 minutes (`spawn_epic`). Each time it creates a new herd with the game's `CreateHerd`. Nothing ever removes that herd, so one more herd made by the mod stays in the world per epic, and every save of the host keeps them. An epic that is alive when the host saves goes into the save too, and after a load it is back as an epic the mod no longer tracks. CLAUDE.md: "Our objects never go into a save: puppets, proxies, mirrors and the herds made for them".

### Where
- `spawn_epic` creates the herd (epic archetype 0xFC811218, `+0xF0 = 0`) and the creature: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L960-L1012
- `epic_tick`: when the epic dies only the pointer is cleared, the herd stays; a new one is made 10 minutes later: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L1014-L1031
- Session end and world change do not touch the epic or its herd: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L1116-L1130
- The only save hook in the mod is the Space allies one; nothing runs around a tribe save.

### How to reproduce
1. Host and guest in the Tribe stage, co-op running for 8 minutes or more (or `coop.tribe epic` to spawn one).
2. While the epic is alive, save on the host (pause menu, Save). Optionally kill it first and wait for the next one, to get several herds.
3. Leave the session and load that save on the host.
4. The epic is in the world again, untracked by the mod. With a herd count (`coop.npc` / an object dump of the herd noun) the herds made for earlier epics are still there.

### Expected
Neither the epic's herd nor the epic itself is in the player's save. Each herd is destroyed when its epic is gone or the session ends, and around a save the epic and its herd are taken out and put back.

## Root cause
**What happens**

- Per epic, one herd object of the mod remains in the planet's world after the epic is killed. The herd is not referenced by the mod afterwards, so it is never destroyed.
- A host save (pause menu, quit save, or the share save for a joining guest) writes the world with these herds and, if the epic is alive, with the epic creature.
- After a load (or a restart and load without co-op), the epic stands near the village as a normal epic of the game. It is not the co-op event any more: no shared 80 food, no notice. The extra herds stay in that world for good.
- Unlike the Creature-stage herds of BUG-0020, these herds use a real epic archetype. What the game does with a saved epic herd after a load (respawn an epic, or nothing) is not known.

### Confidence
PLAUSIBLE, severity low. It is CONFIRMED by reading that the herd is never removed and that nothing happens around a save. That the Tribe save serializes these herds and the epic is inferred from the Creature stage (docs/re/creature.md section 12, BUG-0020) and not checked in the game. A second reviewer verified the reading.

## Fix
Fixed on fix/w3-civ (421bfe0):
- `spawn_epic` reuses an epic herd of ours of the same species (epic archetype 0xFC811218, no nest,
  not the avatar's, target size 0; found in the game's object list, so also one a save brought
  back) instead of a new herd per epic; the herd's target size is set to 0 right after the epic is
  created and the herd is disabled when the epic is gone (the game enables it again within seconds,
  target 0 holds: it spawns nothing).
- A live co-op epic is destroyed (`game::destroy_creature`) before every game save
  (`tribe::before_save`, called from the one SaveGame hook through the new `spaceext::on_save`
  observer list) and at the end of the session; the next epic comes a minute after a save.
- `coop.tribe fix epic_herd 0`: the old behaviour (tests).
Not done: a game herd cannot be destroyed (no known function, docs/re/creature.md 12), so one quiet
epic herd per species stays in the world and in saves; saves made before the fix keep their herds.

## Verification
`coop_tribe_epic.py` (suite `tribe_epic`), part 2: three epics spawned and killed, then one alive
at a real save (`coop.save`); counted in the game's object list (`coop.tribe epic herds`). FAIL with
`--nofix` (3 herds, enabled, the epic still in the world after the save), PASS with the fix (1 herd,
the planet's own epic only after the save), lane 7, 2 of 2.

### Related
- #45 (chiefs stay 1.3x big after the session ends, and the epic creature state survives a leave or a load)
- BUG-0020 in docs/tickets (our objects: puppets, proxies, their herds are written into saves, Creature stage)

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #100.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-civ (421bfe0), test tribe_epic (FAIL with --nofix, PASS with the fix). The fixture planet has one epic of its own: the test counts relative to it.
- 2026-10-28: released in 0.7.3.0.
