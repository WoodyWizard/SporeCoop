---
id: BUG-9076
title: Destructible city buildings miss reserved-founder health and incoming damage scaling
status: fixed
kind: bug
stage: Civilization
reported: 2026-11-06
source: Duo 19 four-seat native building combat test
role: host guest
versions: duo19-dev
fixed_in: unreleased
branch: feat/lineages-civ-combat
commits: ce261273
tests: tools/tests/duo_civ_building_combat.py
signature: Rival building max 800 stays 800 and incoming hit stays 10 at four seats
docs: docs/re/civ.md
related: BUG-9072
---

## Report

In the joined Clyth fixture with four reserved seats, a rival military
vehicle dealt 10 damage to the founder's house instead of 11.5. A rival
building retained 800 maximum HP after taking a founder's hit, instead
of the approved 1,160. Both processes reported zero native faults.
The failing run is build/local/lineages-buildings-b.log.

## Root cause

Only vehicle and turret damage hooks enter the reserved-founder scaler.
Ordinary buildings use a separate cCombatant at +0x120. Building
snapshots carry destroyed state but omit partial health and maximum HP.

## Scope

Destructible houses, factories and entertainment buildings. City Halls
turn qualifying damage into capture progress and require a separate rule.

## Fix

Reuse the host's vehicle/turret combat scaler for destructible buildings.
Track live building health by serial and normalize before native saving
or leaving. Include current HP and scaled maximum in building snapshots;
the guest waits for its native planner before applying them. The switch
is coop.fix civ.building_combat. Duo 20 requires matching peer builds.

## Verification

FAIL: lineages-buildings-b.log on the installed Duo 19 DLL.
PASS: lineages-buildings-c.log and lineages-buildings-d.log on Duo 20.
Both runs measured 800 to 1,160 maximum HP and 11.5 incoming damage,
with matching native health on both peers. The second saved a 60% wound,
restored 1,160 maximum after serialization and 800 after leaving.
PASS: lineages-buildings-cold-b.log loaded that saved building in a new
process at 800 maximum and 479.99997 current HP (expected 480).
The first cold launcher queried coop.savedb before the plugin registered
its commands; the test now waits for the galaxy menu before that query.

PASS: lineages-buildings-turret-a.log covers existing vehicle/turret damage,
wall placement/removal, guest wounds and health normalization after leaving.
PASS: lineages-buildings-e.log uses the installed, stripped Duo 20 DLL and
also captures the damaged rival building. Both peers restore native maximum
health once it belongs to the team. All game runs ended with zero native
faults and normal quit acknowledgments.

The Windows unit executable reports ALL OK. RE, documentation, glyph and
ticket checks pass. lineages-duo20-package.log verifies actual ZIP install,
uninstall loader restoration, preserved save/settings sentinels and reinstall
in a disposable fixture. No user game installation was changed.

## History

- 2026-11-06: Reproduced missing building scaling, corrected the native damage
  path and snapshots, and qualified both peers, saving and cold restart.
