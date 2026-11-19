---
id: BUG-1325
title: Tribe -> Civ: the host's game faults in TribeStage_Graduate (DestroyInstance of a null food mat +0x260) after the City Hall editor, then thousands of caught faults
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in: unreleased
branch: fix/w57-crashes
commits: f2b44601 1defae3e 0ffc91f2 3ca4a068
tests: line_stages, tribe_city_hall_race, tribe_city_hall_race_off, tribe_city_hall_race_crash
signature: exception 0xC0000005 at 0x00B22566 cGameNounManager::DestroyInstance+0x6 (data 0x20), calls 00CDD43D 00CDECAC 00CDF11C (TribeStage_Graduate+0x34D < TribeStage_EnterUpdate+0x4CC)
docs: docs/re/tribe.md#tribe-graduation-cinematic, docs/06-testing.md#testing-tribe-city-hall-race
related: BUG-0856, BUG-0585, BUG-1276
---

## Report
line_stages, lab 20261119-120449 (lane 4, fix/w57-savestrip on rel/0.7.16 79ee9769): the host (Alice) came back from
the end-of-Tribe City Hall editor (12:28:55 "back from the editor 99E92F05 in the end of Tribe"), 1 s later its game
faulted at DestroyInstance+0x6 called from TribeStage_Graduate+0x34D (the run's report-20261119-122900-i7.txt is kept
next to the run's logs), then the campaign counted 279, 3105, 1250, 1999 caught faults on the host. No save in between.
Not seen in line_stages of 20261119-095414 / 20261119-114816 (the same step passed).

## Root cause
Observed (Ghidra 2026-11-19): TribeStage_Graduate 0xCDD0F0 at 0xCDD41E reads the player tribe's food mat +0x260, calls
its vt 0 only when it is not null, and then pushes it to cGameNounManager::DestroyInstance unconditionally (0xCDD430):
a null +0x260 faults at DestroyInstance+0x6. The mat was null at the graduation. Not analysed further (found by
fix/w57-savestrip, outside its task): who cleared +0x260 before (BUG-0585 notes the mat is gone after the City Hall;
a second Graduate run, or a mod path), and whether the guest's own City Hall at its arrival (BUG-1276) plays a part.

Analysis (2026-11-19, fix/w57-crashes; docs/re/tribe.md#tribe-graduation-cinematic). Not our mirrors, strays or
species: the mat was null because the graduation ran a SECOND time.
* Observed (Ghidra): TribeStage_Graduate 0xCDD0F0 destroys and clears +0x260 and ends with
  PlayCinematic("TRG2CVG_ShowCityHall", 1, 0, ...) without the queue flag; the cinematic manager's start 0xADFCE0 drops a
  cinematic asked for while another plays. ShowCityHall's end (0x44F1189) is what submits the vehicle editor.
  NpcTribeController_FormTribe 0xCC6440 plays TRG_PRE_FirstTribeAppears when it forms a planet's first NPC tribe.
* Observed (logs of 20261119-120449): the goal bar was filled by the test's shortcut ~60 s into a new Tribe stage, before
  the first NPC tribe; that tribe was formed right when the host came back from the City Hall editor (12:27:17.9), a
  20 s cinematic with a caption played at the new village (the host's trace position 6 m from it), no vehicle editor
  came (the passing runs: ShowCityHall 60 s, then the vote for F670AA43), the bar was still full and the driver's
  click at 12:27:45 started the stage card, TRG2CVG_PreEditor and the City Hall editor (99E92F05) again; back from it the
  second Graduate faulted (its call at 0xCDD438, return 0xCDD43D, is the crash's first stack entry).
* Inferred: the cinematic was TRG_PRE_FirstTribeAppears (not logged; FormTribe's, timing and place).
* Player reach: FirstTribeAppears needs a planet without NPC tribes, which a full goal bar excludes, so this exact case is
  the tests' shortcut; any other cinematic playing at the graduation gives the same stall, and the stuck player's next
  advance the crash.

## Fix
* `tribe.city_hall_cinematic` (mods/coop/tribe_graduation.cpp, a hook of SporeCoop::TribeStage_Graduate): a cinematic
  that plays when the graduation starts is ended first (EndCinematic, its end message goes out as on its OK), so
  ShowCityHall plays with its actors and the vehicle editor follows. Off: 0.7.15.
* `game.destroy_null` (gamefixes' DestroyInstance hook): DestroyInstance of a null object is skipped (it faulted at +0x6
  writing obj +0x20): a second graduation goes on (ShowCityHall, the vehicle editor, Civ). Off: the game's call.
* `coop.tribe graduation` (counts), `coop.tribe gradcine <name>` (tests: a cinematic right before the next graduation).

## Verification
Test tribe_city_hall_race (coop_tribe_city_hall_race.py, lane 9, fix/w57-crashes worktree):
* PASS: out/lab/20261119-141549:tribe_city_hall_race (the planted TRG_TotemPole_Conquer 5418B038 ended at the
  graduation, ShowCityHall 10792A72 played, the next request F670AA43, both games in Civ, 0 faults).
* FAIL (twin): out/lab/20261119-141549:tribe_city_hall_race_off (tribe.city_hall_cinematic off): ShowCityHall missed, the
  host stood in Tribe 78 s, the second advance gave a second City Hall 99E92F05 and a graduation without the mat;
  game.destroy_null skipped the null destroy from 0x00CDD43D (the crash's own call site) and the game went on to Civ.
* FAIL (twin): out/lab/20261119-141549:tribe_city_hall_race_crash (both off): the stall, the second City Hall, then
  68578 faults 0xC0000005 at DestroyInstance+0x6 (data 0x20), calls 00CDD43D ... 00CDECAC 00CDF11C: the signature of the
  report; the graduation called again every frame (49150 times), the host never left Tribe.

* PASS on the tree merged with rel/0.7.16 (80c722d4): out/lab/20261119-144809:tribe_city_hall_race.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w57-crashes (cause found: a dropped ShowCityHall, a second graduation; two switches, test with two twins).
