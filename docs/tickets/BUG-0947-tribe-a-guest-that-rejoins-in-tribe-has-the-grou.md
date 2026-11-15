---
id: BUG-0947
title: Tribe: a guest that rejoins in Tribe has the ground round a village the host made live 4.5-4.9 m lower than the host (its copy stamps the ground differently)
status: fixed
kind: desync
stage: tribe
reported: 2026-11-11
source: final check of 0.7.12 (lab 20261111-151535, campaign, Carol after its rejoin)
role: guest
versions: 0.7.12
fixed_in: 0.7.13
branch: fix/w48-tribe-mirrors
commits: b781e846
tests: tribe_form_ground_rejoin
signature:
docs: docs/re/tribe.md#tribe-village-load-stamp, docs/06-testing.md#testing-tribe-mirror-looks
related: BUG-0332, BUG-0945
---

## Report
Found in the campaign of the third final check of 0.7.12 (lab 20261111-151535; no player report). Carol left and
joined again in the middle of the Tribe play (s 1473-1485). From then on the campaign's sync note says for Carol
only: "ground height differs (objects in place on their own ground): Carol n303: centre distance host 527.2 guest
522.3" and "n297: host 525.8 guest 521.3": the ground under two animals is 4.5-4.9 m lower in Carol's game. None of
the three earlier final checks has the note (their rejoin fell into Cell and Space).

## Root cause
Proven (Ghidra + lab, tribe fixture, two games). A village has two ground stamps: the one made with it (0xC9DEE0) and
the tribe's own (cTribe +0x19D4, 0xC91890, about 50 m wide), which only the live Creature -> Tribe change and a load
(for a village saved without it) make; the NPC tribe controller's forming and SpawnNpcTribe (the guests' copies) do
not. So the games that saw a village formed had one stamp there and a game that loaded the world later two: after the
guest's rejoin its ground 50 m round a village the host had formed was 7.95 m off (lab 20261112-094334, -100542; 0.00
before the rejoin), its stamp list one entry longer (the load's "ApplyStamp from 0x01091981" of the report = 0xC91981:
terrain_diag printed every caller 0x400000 too high, corrected). The same stamp applied live on the host by hand gave
the same id and 0.00 m at every point. Not BUG-0332's live-versus-loaded rendering.

## Fix
`tribe.village_load_stamp`: every game in Tribe gives a tribe whose +0x19D4 is 0 its stamp through the game's own
0xC91890 (`SporeCoop::cTribe_ApplyGroundStamp`) a second after the tribe appeared; a later load then adds nothing.
`coop.tribe` tribe lines print `stamp=`; `coop.terrain stamp` (new, RE) applies a stamp with another's transform.

## Verification
Test `tools/tests/coop_tribe_form_ground.py --rejoin` (suite `tribe_form_ground_rejoin`): the host's game forms a
village, the guest makes its copy, leaves and joins again; the two grounds within 50 m of the village must stay
within 0.5 m.
* FAIL with `SPORE_FIX_OFF=tribe.village_load_stamp`: out/lab/20261112-101524 (7.95 m on the 50 m ring); before the
  fix out/lab/20261112-094334.
* PASS: out/lab/20261112-101319, out/lab/20261112-101731 (0.00 m).

## History
- 2026-11-11: opened (fix/w47-final12c). For 0.7.13. Reproduce: a four-player campaign with the rejoin in Tribe after
  `coop.tribe form` made a village, then `coop.terrain` on both games round the village.
- 2026-11-12: fixed (fix/w48-tribe-mirrors): a village made live gets the tribe's ground stamp too.
- 2026-11-12: released in 0.7.13.
