---
id: BUG-0399
title: Tribe: the guest shows some of the host's animals 0.75-1.25 m under its ground for 20-30 s after joining (driven at their own height)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-01
source: lab monitor (feat/w32-monitor-diag, proxies rule), lane 6
role: guest
versions: unreleased
fixed_in: 0.7.3.4
branch: fix/w33-proxy-ground
commits: d185b5b, 351daac, 998898b, 537484a
tests: tribe_coop, chaos_tribe (monitor rule proxies)
signature:
docs: docs/re/tribe.md#tribe-proxy-ground, docs/06-testing.md#testing-proxy-ground
related: BUG-0329, BUG-0366, BUG-0406
switch: npc.proxy_ground
---

## Report
The invariant monitor's `proxies` rule (guest trace line `proxy_under=`, sampled 4 times a second at
the start of the frame) found NPC proxies under the guest's own ground (cPlanetModel::GetHeightAt) right
after the guest entered the host's Tribe world:
* out/lab/20261101-123454-feat-w32-monitor-diag tribe_coop: `proxy_under=4` on the lines at s 20 and
  s 30, 0 from s 40 on; `npc: proxy 14 (species 16DE4FEA) 0.75 m under this game's ground ... for 1.0 s,
  the host's NPC 0.6 m from it (driven)`, `proxy 2 (species 0680AA69) 1.25 m under ... for 4.7 s, the
  host's NPC 0.4 m from it (driven)`.
* out/lab/20261101-120947-feat-w32-monitor-diag chaos_tribe: 1 proxy under on the lines at s 20 and 30.
The player tribe's members on the same guest read 0.00 m (`agl=` on the trace lines): the query matches
the ground the members stand on.

## Root cause
Observed (fix/w33-proxy-ground, lane 7; the under line now says where the proxy was made): only right after
cold joins (about 1 in 5 cold tribe_coop / chaos_tribe runs with the fix off); the proxies were made ON the
guest's ground (`made 1.8-5.9 s ago at 0.00 m over the ground`) and were 1.3-1.7 m under it 0.8-6 s later,
placed on the host's path (walking) or standing (`driven`); 4 of 5 of one herd of the fixture. So the first
inference (made at the host's absolute height) was wrong.
Not reproduced in warm games: a proxy put 1.2 m under the ground on purpose (`coop.npc sink`) is grounded by
the game within 0.6 s (also at 4 fps); slope walks, proxies made again, terrain sampled after the join: none
under (docs/re/tribe.md#tribe-proxy-ground).
Inferred: right after a join the guest's game does not update (ground) its creatures for seconds, while the
guest places them at their own distance from the centre (own_height) on rising ground; why some stayed under
for 20-30 s is not known.
~ superseded (2026-11-01): the inference "made at the host's absolute height":
Inferred, not checked: Tribe proxies are driven by `drive::step` with `own_height` (BUG-0329,
drive_plan.h `at_own_height`): the target keeps the proxy's own distance from the planet's centre. A
proxy spawned or teleported at the host's absolute height where the guest's ground is higher starts
under it and keeps that height; the game does not lift a creature placed under its terrain
(docs/re/creature.md#creature-puppet-ground). They come up after 20-30 s (a walk, a teleport).

## Fix
`npc.proxy_ground` (driver.h `out_of_ground`, npc.cpp): a Tribe proxy's walk is placed onto this game's ground
(`WalkPlace::ground`: 5 cm into it when within 3 m under to 0.5 m over, BUG-0366's on_own_ground),
`drive::step(.., ground)` teleports a standing proxy found more than 0.25 m (up to 15 m) under the ground onto
it, and a new proxy is made on the ground (the last one inferred only: a host position under a higher guest
ground, BUG-0329). `coop.fix npc.proxy_ground off`: own height only (0.7.3.4). The suite.py ALLOW_ALL entry is
removed.

## Verification
The monitor's `proxies` gate (no ALLOW_ALL entry any more) in tribe_coop / chaos_tribe, lane 7. The bug shows only
after cold joins and not in every one, so the evidence is a rate, not a deterministic test (a deterministic plant,
`coop.npc sink`, does not reproduce it: the game grounds such a proxy within 0.6 s; slope walks did not either).
* FAIL with `SPORE_FIX_OFF=npc.proxy_ground`: out/lab/20261101-140615 tribe_coop (gate: `1 NPC proxies under the
  ground (Tribe) on 2 lines from s 20`; proxy 27 1.32-1.66 m under, chaos_tribe's guest proxy 6 1.41 m under on one
  line); 20261101-150122 (8 cold runs: proxies 14 and 19 1.30 / 0.51 m under on one line each, under the gate's 2
  lines); 20261101-142745 (4 runs, none). Before this branch: feat/w32-monitor-diag 20261101-123454 tribe_coop
  and chaos_tribe, 20261101-120947 chaos_tribe.
* PASS (fix on): 0 proxies under the ground on any trace line in out/lab/20261101-141252 (tribe_coop and chaos_tribe
  3 of 3 each), 20261101-153010 (tribe, tribe_coop, tribe_play, chaos_tribe, tribe_walk, tribe_walk_player,
  npc_walk_tribe, tribe_epic, play_tribe 10 min, campaign_tribe 4 players), 20261101-161258 (tribe_coop, chaos_tribe
  2 of 2 each, cold).
* Not shown: a run where the fix visibly acted (`npc: proxy N was X m under ... put onto it` lines, since 537484a): none
  in the PASS runs after that commit either (the state is rare).

## History
- 2026-11-01: opened.
- 2026-11-01: fix/w33-proxy-ground: the made-at-the-host's-height inference disproved, fixed by npc.proxy_ground (allow entry removed).
- 2026-11-02: released in 0.7.3.4.
