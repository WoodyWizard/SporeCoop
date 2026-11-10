---
id: BUG-0793
title: Feature idea: co-op Galactic Adventures (shared run, lobby, objectives, NPC authority, results)
status: open
kind: feature
stage: Adventure
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-main (fork release 0.7.4.0, preview 12)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0789, BUG-0790, BUG-0863
---



## Report
Feature idea, not a bug. Today an adventure in co-op only shows each other's captain as an echo puppet (docs/07-modes.md:
everyone launches the same adventure themselves; puppets meet once the adventure keys match; NPCs, objectives, items and
acts are not shared). The README roadmap lists "co-op adventures". The fork jwhans0n/SporeCoopGA built a real co-op mode
for Galactic Adventures and published it as its fork release 0.7.4.0 (protocol 15, later rebased on our 0.7.8 on branch
`sync/upstream-0.7.8-main`, protocol 27).

## Root cause
Not a bug. What the fork built (state from its docs, not verified by us):
* Flow (works, two-player tests on native Windows): the host picks an adventure in Sporepedia and presses Play; every player
  runs the native captain wizard (CaptainWizard* functions); a shared lobby card with every captain's portrait, the host
  transfers missing adventure and captain resources (DBPF, MSG_ADVENTURE subkinds 64..71), everyone presses Ready; after
  beam-down a "Continue" confirmation holds clocks until all confirmed; Replay and host Done for the whole party; first
  host-confirmed win/loss seals the run for everyone (results coordinator).
* World authority (works with limits): the host owns NPC AI, damage, objectives, act changes (CheckGoalProgress,
  CompleteAct, JumpToAct, SetCurrentAct); guests keep their native scenario instances, NPC AI suppressed through an
  NPCTickAI filter that keeps the native visual / locomotion update; actors bound by (scenarioMarkerID, hostRuntimeID)
  via GetMarkerForObject / GetObjectForMarker; a reliable full actor roster removes host-absent instances; 10 Hz snapshots,
  static objects only after a reliable baseline (host-to-guest traffic 155 kB/s -> 25 kB/s in their test); followers walk
  with native MoveTo waypoints between samples (position error p95 ~8 m for vehicles).
* Interactions: keys and gates, explosives (mines, barrels, bombs, grenades: host's native activation F24730), pickups and
  boosts per captain, teleporters, Give to NPCs and containers (BuildingInventoryGive, VehicleInventoryGive), held items
  and throws / catches, talk dialogs opening on every player with a group "Finished" confirmation, act music.
* Rules: difficulty settings, friendly fire 0/25/50/100 %, downed state with 6 m / 3 s revival or automatic revival after
  30 s, unanimous pause (shared clocks stop only when everyone has the Esc menu open), enemy targeting of guest captains in
  authored player-team attack instructions (the guest puppet's political id differs from the local captain's).
* Not done: entry from the Space stage, late join / reconnect in a run, four-player coverage, Steam and other exe variants
  (their adapter is enabled only for the exact dc04aee5 exe), Linux / Wine runs (validated on native Windows only).
* Size: about 13 600 added lines in mods/ (40+ files mods/coop/adventure_*.cpp/h and mods/coop/net/adventure*.h/cpp) and
  about 16 700 lines of tests (tests/adventure_*_tests.cpp, tools/tests/coop_adventure_*.py), 202 symbols in
  `data/symbols/overrides_ga_dc04aee5.sym` plus 19 in `overrides_world_dc04aee5.sym`, RE notes docs/re/adventure.md (968
  lines), docs/re/adventure-world.md (2303 lines), docs/re/build-dc04.md (888 lines). Licence: MIT, "Copyright (c) 2026
  WoodyWizard" (our own licence file kept), so code can be taken with attribution.
* Their shared-code changes to look at with it: game.cpp update hooks (set_extra_update_filter, set_npc_ai_filter,
  npc_ai_subject, blocked update handler, update observer), PlayerState gets scale + adventure_run + captain_selection +
  movement_generation (wire change), modes.cpp kScenario: puppet spawned only with the player's own captain species,
  puppet scale = the owner's actual scale, no path fix and no creature_hp in adventures.

## Fix idea
From the fork (jwhans0n/SporeCoopGA sync/upstream-0.7.8-main, release 0.7.4.0 / preview 12); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
Options: (1) take the fork's adventure modules as a whole (MIT) after a review against our rules (lab on Wine, two-instance
tests through our lab, symbols only through H->sym, the deferred destroy path), with the authors' cooperation for
upkeep since it is 13k lines of code; (2) adopt pieces cheaply: the RE (sym entries, docs/re facts), the lighting and
TakeDamage fixes (BUG-0789, BUG-0790), the per-captain scale of the puppet; (3) keep echo puppets and point players to
the fork. The protocol and message numbers must be re-allocated against ours (they use MSG_ADVENTURE = 65, MSG_EDITOR = 66,
MSG_ACTIVITY = 55; ours collide, e.g. our 65 = MSG_NPC_MEAT). Needs an owner decision and a plan entry for 0.7.13 or 0.8.

## How to test
Their suites as a model: tools/tests/coop_adventure.py (lobby, beam-down, Continue), coop_adventure_outcome.py
(shared win/loss), coop_adventure_enemy_targeting.py (an authored enemy attacks the guest captain), coop_adventure_pause.py,
coop_adventure_pickups.py. For us: two lanes, the same stock adventure (Defend the Crystal Mine), assert on the guest's
game the goal counter and the NPC's target set by the host's AI.

## Fix
## Verification

## History
- 2026-11-10: opened.
