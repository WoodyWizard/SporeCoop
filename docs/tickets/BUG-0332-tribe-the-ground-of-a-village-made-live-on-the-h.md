---
id: BUG-0332
title: Tribe: the ground of a village made live on the host is up to 5 m higher or lower on the guests that load its save
status: fixed
kind: desync
stage: Tribe
reported: 2026-10-30
source: lab 20261030-210914 campaign#1 / campaign#3
role: guest
versions: main be725db
fixed_in: unreleased
branch: fix/w45-village-ground
commits:
tests: tribe_village_cadian (coop_tribe_village_ground.py --fixture=creature_cadian --map --terrain), tribe_village_ground, tribe_form_ground (coop_tribe_form_ground.py), campaign_ct (CAMPAIGN_GROUND=1, CAMPAIGN_FORM=1)
signature:
docs: docs/re/tribe.md#tribe-ground-height, docs/re/tribe.md#tribe-village-ground, docs/re/tribe.md#tribe-terrain-stamps
related: BUG-0329, BUG-0608
---

## Report
The probe lines of lab 20261030-210914 (campaign#1 lane 2, campaign#3 lane 7): the host's tribe
members and nearby animals and their copies on every guest differ only in their distance from the
planet's centre, by 3.9-5.3 m (host lower in campaign#3, higher in campaign#1), the same on all
guests. In campaign#3 the guests' members stood at 516.05 m in two places, the host's at 510.9-511.0.

## Root cause
Observed (lab, GOG 2024, creature_cadian fixture = the campaign's own planet; docs/re/tribe.md#tribe-terrain-stamps):
* At the live Creature -> Tribe change the host's game applies ~56 terrain stamps (Terrain ApplyStamp 0xFA6C10, from
  0x0109DFBF / 0x01091981 the player village, 0x010DDDEF the tribe spawn markers, 0x0106AD74 ~50 more): flat plateaus
  that cut or fill the ground by up to 25 m. The guests load the host's share save: their stamp list is the host's,
  byte for byte, yet their ground differs round some stamps by 0.4-14 m (lab 20261106-033400, -034853, -040512,
  -042556). Before the change (Creature) the two grounds are identical at all 1499 sampled points.
* The host's own load of its Tribe save gives exactly the guests' ground (lab 20261106-043244, 12 of 12 points). So
  the game renders the stamps it applies live differently from the rebuild every load of the same list makes: the
  host keeps its live ground, every guest has the loaded one. In the campaigns the host's members walked onto such
  a patch (a new NPC village, "made it ... at 5.1 m"), hence 5.1 / 5.9 m.
* Not the causes (measured): the stamp lists, the terrain manager's fresh-load flag (+0x1CD = 0), the height
  parameters, a late share save (the host's terrain is final 0.9 s into the stage). A rebuild started by the mod
  (sphere+0xA48, or vt 0x80 + vt 0x114 + 0xF9B860 as the planet editor) gives a third ground (lab 20261106-043944,
  20261106-045023).
Inferred: the height generator renders stamps onto the heights it has, so only a load (from the planet's base)
gives the canonical ground; why the live Tribe-start render differs inside the generator is not traced.

## Fix
`tribe.host_reload` (world.cpp, coop.cpp): after a live Creature -> Tribe change the host loads its own share save
once it has gone out to every guest (the game's own load via the galaxy menu, as the guests do), so every game has the
ground any later load of that world gives. `coop.fix tribe.host_reload off`: the host keeps its live ground. Side
effect: the host has one more loading screen (~10 s) at the start of Tribe and the guests read "Alice (host) left for
the main menu: you play on alone until Alice is back" for those seconds. Probes: `coop.terrain [list | raw <id> |
state | rebuild | dirty | regen | reapply <from>]`, `coop.tribe markers`, `coop.tribe npc_tribe_at` (terrain_diag.cpp,
tribe.cpp).

## Verification
coop_tribe_village_ground.py --fixture=creature_cadian --map --terrain (`tribe_village_cadian`): the players' way from
the campaign planet's Creature to Tribe; then the game's own ground (`coop.ground`, cPlanetModel::GetHeightAt) on both
games at 1499 points within 45 degrees of the village and on rings to 100 m round every village and spawn marker.
PASS: every point within 0.3 m.
* FAIL with `SPORE_FIX_OFF=tribe.host_reload`: out/lab/20261106-045914 (62 points off, up to 2.36 m; 1.99 m at a
  spawn marker); also before the fix out/lab/20261106-033400 (3.08 m), out/lab/20261106-040512 (13.9 m).
* PASS: out/lab/20261106-045849 (0 of 1499 points off, largest 0.00 m; the host loaded its share save once).
* Regression: see the report of fix/w45-village-ground (out/REPORT.md).

## History
- 2026-10-30: opened.
- 2026-11-03: looked at on fix/w41-tribe-world, no new finding.
- 2026-11-05 (fix/w45-tribe, T2 of 0.7.9): not reproduced on the `creature` fixture. New probe test
  tribe_village_ground (coop_tribe_village_ground.py): the players' live Creature -> Tribe change, then the game's own
  ground (`coop.ground`, cPlanetModel::GetHeightAt) on both games at the new village's centre and on rings of 10, 25,
  50 and 100 m (8 directions): host and guest within 0.20 m everywhere, twice 20 s apart (out/lab/20261105-232737,
  PASS). Observed: the guest's ground is the host's there; the live village does not change the ground in a way the
  save loses on this planet. Inferred: the campaign's 4-5 m came from its own planet (made in the campaign's Cell and
  Creature stages, not a fixture), so the probe needs such a world to show it: a campaign run (`coop_campaign.py
  --from cell --to tribe`) with this probe's sampling is the next step. Stays open (no failing run on a fixture).
- 2026-11-06 (fix/w45-tribe): reproduced in 4-player campaigns Cell -> Creature -> Tribe (campaign_ct, lanes 9 / 10),
  2 of 4 runs: out/lab/20261105-233903 (seed 880673564): the host's members at 510.9 m from the planet's centre, every
  guest's copy at 516.0 (5.1 m higher, the same on all three guests; each on its own ground, sideways 0.0-0.8 m);
  out/lab/20261106-000530 (the same seed, another planet pick): host 508.1, guests 514.0 (5.9 m). Not in
  out/lab/20261105-235245 and out/lab/20261106-001813. So it is the campaign's own planets, not the fixtures', and
  the guests were higher both times (inferred: the host's live game lowers / flattens the ground under the village it
  makes, and the save the guests load does not carry that). The campaign has a ground probe for it now
  (CAMPAIGN_GROUND=1: `coop.ground` of every machine at a tribe member / a player and 15-200 m off, noted when a
  guest differs by more than 0.5 m; CAMPAIGN_SEED=<n> replays a seed through the lab); it was on only in runs
  without the difference, so whether the difference is local to the village is not measured yet.
  Leads in the game (strings only, ? roles): `mTerrainStampsToRemove` (0x14655A8, registered by 0x1322A30) and
  `mAppliedStamp` (0x1469174): terrain stamps a placed object applies, and a list of stamps to take off, saved
  with the planet. Consequence found on the way: BUG-0608 (relayed world effects at the sender's absolute height).
  Stays open: the cause is not shown and no fix is made; members, animals and buildings stand on each game's own
  ground, so each player sees a consistent village.
- 2026-11-06 (fix/w45-village-ground): seeds of the failing campaigns replayed with the ground probe (lab
  20261106-024336, -025638: not reproduced, the planet's NPC villages formed elsewhere); the failing campaigns'
  guests had logged "made it ... at 5.1 m / 5.9 m" for the host's new NPC village: the spot. Reproduced on the
  creature_cadian fixture with ground maps; terrain stamp hooks; root cause: live stamp render != loaded render (the
  host's own reload gives the guests' ground). Fixed with tribe.host_reload.
