---
id: BUG-0602
title: Civ: a city building stands at another place in the guest's game (detector planet:building_industry / house / entertainment, campaign_space)
status: fixed
kind: desync
stage: Civilization
reported: 2026-11-05
source: detector, campaign_space lab 20261105-132636 (fix/w44-world-state2); rel/0.7.7 final 20261105-015350
role: guest
versions: 
fixed_in: unreleased
branch: fix/w45-placement
commits: a42f6e5f, 03b802ec, 76f7ab66
tests: civ_city_layout (coop_civ_city_layout.py)
signature:
docs: docs/re/civ.md#civ-capture-layout
related:
---

## Report
Found by the detector while checking BUG-0593 (fix/w44-world-state2). campaign_space with `SPORE_DETECTOR=20`, lab
20261105-132636: `Civilization:planet:building_industry` in 24 of 65 Civ comparisons ("9 on the host's planet, 9 on
the guest's; 1 only on the host, 1 only on the guest"), the same for building_house / building_entertainment (6);
also in the rel/0.7.7 final check (20261105-015350, building_industry 30 samples, 24 %). The counts are equal: one
building of a city stands at another place (more than 8 m off) in the guest's game. Not the owner (no planet_owner
row after BUG-0593's fix). ~ Inferred: a building the host's city grew or the planner moved went to another slot on
the guest (SnapBuildingToSlot picks the free slot nearest the given position, docs/re/civ.md 1).
Reproduce: `SPORE_FREE_LANE=<free> SPORE_DETECTOR=20 tools/lab.py run campaign_space --lane N`,
`tools/detector_report.py <run>`.

## Root cause
Observed (campaign_space with the new diagnostics, lab 20261106-064018): right after the campaign's capture of city 4
(a city it had founded with SpawnNextCity) every guest logged every 2 s `the host's building 018EA2CC of city 4 at
(-319.0 -310.6 -239.6) went to (-317.0 -316.8 -234.0) here, 8.6 m off` (buildings_fixed=+224/-224 at the end; the
detector's planet:building_industry in 27 samples). `coop.civ slots 4` / `blist 4` read live from both games in that
run: the host's city 4 had a new layout after the capture (all 12 slots elsewhere, the industry in its slot 5 at
(-319 -311 -240)); the guests kept the founding's layout. So the inferred loop of the report is confirmed:
reconcile_buildings found no local building within 4 m of the host's, destroyed the guest's (extra) and rebuilt the
host's (missing) with SnapBuildingToSlot, which took the free slot nearest the host's spot, the guest's own 8.6 m away.
Inferred (not traced): the relayout comes with the captured city's new models (City_SetPartsOwner 0xBE4C80, the
owner's City Hall), which a guest's ChangeOwner without Capture's +0x2E2 flag does not run; a two-game
`coop.civ capture` did not move the host's layout (lab 20261106-062912), so the trigger needs what the campaign has
(the capture caption showing the city, ?).

## Fix
`civ.city_layout` (civ.cpp to_host_spot): a building the guest makes for the host's (the reconciliation, MSG_CIV_BUILD)
and one matched 1-4 m off is put at the host's spot, its cLayoutSlot (+0xC) moved with it, so the slot nearest the
building stays its own (the planner's pick, BUG-0491) and the next snapshot matches it: no destroy / rebuild loop and the
building stands where the host's stands. `coop.fix civ.city_layout off`: the nearest free slot (0.7.8). Diagnostics:
`coop.civ` off_slot= / moved=, the BUG-0602 log lines (both sides), `coop.civ blist`, `bmove` (tests).

## Verification
civ_city_layout (civ_npc; the host founds 3 cities, captures one, moves one building 8.7 m off the guest's slot
with `coop.civ bmove`, the state campaign_space showed, builds 12 more; the game's own building lists of the new cities
on both games for 60 s): FAIL with `SPORE_FIX_OFF=civ.city_layout` out/lab/20261106-065338 (the building 8.7 m off for
60 s, the guest replaced buildings 31 -> 44 times), PASS out/lab/20261106-065813 (matched within 3 s, off_slot=1
moved=1, no more replacing). Detector before: campaign_space out/lab/20261106-064018 (building_industry 27 samples,
+224/-224); after: play_civ out/lab/20261106-080336 (no planet:building row; campaign_space not rerun with the fix).

## History
- 2026-11-05: opened.
- 2026-11-06 (fix/w45-placement): root cause observed (the host's captured city got a new layout, the guest's rebuild loop), fixed with civ.city_layout; runs above.
- 2026-11-06 (fix/w45-placement): regression with SPORE_DETECTOR=20: smoke out/lab/20261106-074134 (14/14), play_tribe out/lab/20261106-073013, play_civ / play_space and the touched tests out/lab/20261106-080336 (9/9; play_civ: no planet:building or scale:vehicle row, the guest's buildings_fixed=+0/-8 off_slot=0).
- 2026-11-06: Discord report 2026-11-06 (0.7.8, discord-2026-11-06/M0ng0r-17): "city's buildings get reshuffled after I place them or switch their places": the guest's buildings_fixed stays +0..+21 until the first capture (s 6279), then climbs +46 / +146 / +443 / +806 / +1202-1201 (s 6370..6700, 3-5 destroy-and-rebuild pairs a second), captures at s 6279, 6424, 6585, 6791: this ticket's mechanism; 0.7.8 has no civ.city_layout. Not covered by this ticket's text: the guest's own buildings moving in its own planner; check with the 0.7.9 fix.
- 2026-11-06 (orchestrator, log sweep): campaign_space (24-30 building_industry rows a run on 0.7.8) not yet run with the fix; it runs in the 0.7.9 final check.
