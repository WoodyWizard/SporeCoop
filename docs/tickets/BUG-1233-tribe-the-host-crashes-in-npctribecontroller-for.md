---
id: BUG-1233
title: Tribe: the host crashes in NpcTribeController_FormTribe (cSpeciesManager_PlayerSpecies, property list of a garbage pointer) after coop.tribe form (dc04aee5:SporeApp.exe Resource::StandardFileFilter::IsValid+0x1FEF)
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-18
source: final check 20261117-154911
role: host
versions: unreleased
fixed_in: 0.7.15
branch: fix/w52-formtribe
commits: 7dcfcb0c
tests: tribe_form_species
signature: dc04aee5:SporeApp.exe 0x00406EFF (IBakeManager vt 0x1C 0x406EF0+0xF, named Resource::StandardFileFilter::IsValid+0x1FEF by the nearest symbol), 0xC0000005 read of species+0x504+8, called from NpcTribeController_FormTribe+0x119 (0xCC6559)
docs: docs/re/tribe.md#tribe-form-species, docs/re/tribe.md#tribe-form-needs, docs/06-testing.md#testing-tribe-form-species
related: BUG-0483, BUG-0587, BUG-0571, BUG-0966, BUG-1238
---

## Report
Second final check of 0.7.15 (lab 20261117-154911, campaign_tribe_noclick, 4 players, lane 3): the host's game crashed
in Tribe at 16:26:15, `0xC0000005 at 0x00406EFF` (read of 0x3EF5DE09, eax = edi = 0x3EF5DE01), call chain
NpcTribeController_FormTribe+0x119 with stale frames PlayerSpecies / GetArrayUInt32 / GetProperty below it, reached
from coop.dll's `coop.tribe form` (the campaign's shortcut when no village is left to raid, goal bar 4 of 5). Not
"three calls without consequences": the host's log has `the command 'coop.tribe' was cut off by a fault the game's
handler caught` at 16:22:05, 16:23:28 and 16:24:51 (one per shortcut), and the crash report lists 457 caught faults,
all at 0x406EFF with the same data address. The campaign's fault finding grew by ~30 every 28 s from 16:18 on (after
the fourth village fell, goal bar 3): the game's own NPC tribe controller update faulted once a second for 8 minutes
before the test command's fourth call was not caught and crashed the host.

## Root cause
Observed (Ghidra, GOG 2024; runtime on lanes 2 / 3; docs/re/tribe.md#tribe-form-species):
* FormTribe 0xCC6440 takes the next tribe's species from the controller's list (+0xD0..+0xD4) at index
  **min(size, tribes - 1)** (0xCC6521: `cmp size, tribes - 1; jl` keeps the size; the docs said min(tribes - 1,
  size - 1), corrected). With as many tribe nouns as species it reads the dword after the list's end and passes it
  + 0x504 to IBakeManager vt 0x1C, which reads [key + 8] at 0x406EFF. The return address 0xCC6559 (+0x119) is right
  after that call. Its only guard before is the property list (key at +0xFC): tribes x 2 <= its count.
* The list is built at the stage start with one species per pair of that property list only for archetypes the
  species manager has a species for (0xCC6160), or from the saved keys on a load (0xCC7140): it can be shorter than
  count / 2. The tribe fixture has 10 / 5 (fine: the controller stops at 6 tribes), **the campaign's own planet (its
  Tribe segment save) has 10 / 4**: at 5 tribes FormTribe reads list[4].
* The game's update 0xCC6A90 calls FormTribe once a second with 3-4 tribes, and with 5 after three goals: in the
  campaign the third goal came with the fourth village's fall (16:18), so from then on every second faulted (caught by
  the game's handler; the update's relationship / raid loop after the call was skipped each time). The test command
  `coop.tribe form` runs inside the frame: the first three faults cut the frame off (BUG-0966 answered with an
  error), the fourth was not caught and the host crashed. So the test command is not the cause, it only made the
  crash visible sooner; a player on such a planet gets the same fault from the game itself (and a crash whenever the
  call is not under a handler or the dword past the list is a readable but wrong pointer).
* Reproduced without planting: the campaign's Tribe save (out/lane3/campaign/20261117-154914/segments/tribe.tgz)
  loaded on lane 2, `coop.tribe form` up to 5 tribes, then every call faulted at 0x406EFF (data 0x50B: 0xFFFFFFFF past
  the list) and the host crashed with the final check's signature (lab 20261118-002353, fix off). With the fix the
  same save held every call, 0 faults (lab 20261118-002106).
Inferred / not known: why that planet has one species fewer (an archetype without a species in the test profile, or
the keys saved), BUG-1238.

## Fix
`tribe.form_species` (mods/coop/tribe.cpp `h_Form`, all roles): before the game's FormTribe runs, `form_species_state`
reads what FormTribe reads (the tribe nouns with the game's own GetData call, the property list count via
`App::Property::GetArrayUInt32`, the species list size); when list[min(size, tribes - 1)] is past the list (and the
property guard would let FormTribe read it) the call is held: nothing is formed, which is also what the game ends with
when its read faults. Decided once a second (the update calls every frame while held, as with tribe.form_pace; a
`coop.tribe form` decides now). `coop.tribe form` adds `(held: tribe.form_species)` and the three numbers;
`coop.tribe form_species` prints them alone. `coop.fix tribe.form_species off`: the game's own read (0.7.14).
Symbols added: `App::Property::GetArrayUInt32` (verified), `SporeCoop::TribeNouns_Cb3` / `Cb4`. Wire bytes unchanged.
Since BUG-1238 (same branch) the held call is made with the list's last species instead (`tribe.form_last_species`): the
hold alone is what `coop.fix tribe.form_last_species off` gives.

## Verification
Test `tribe_form_species` (tools/tests/coop_tribe_form_species.py, tribe fixture, cold): the host's species list made as
short as the campaign planet's for the fixture's tribes (2 species, 0xFFFFFFFF past them as in that save), one
`coop.tribe form` (2 -> 3 tribes), then 12 s of the game's own update calls at 3 tribes, then the command once more.
* FAIL with `SPORE_FIX_OFF=tribe.form_species`: out/lab/20261118-002824 (lane 3): 11 faults in 12 s at 0x406EFF from
  the game's update (`(new faults, tribes) every 2 s: [(1, 3), (3, 3), ..., (11, 3)]`), then `coop.tribe form`
  crashed the host: crash report 0x00406EFF, FormTribe+0x119, the final check's signature.
* PASS: out/lab/20261118-002819 (lane 2): 0 faults, 3 tribes, `held: tribe.form_species`, list restored.
* Again with the other tests that call `coop.tribe form` or touch FormTribe: out/lab/20261118-003556 (lanes 2 / 3):
  tribe_form_species, tribe_copy_marker, tribe_guest_tribes, tribe_form_pace, tribe_form_ground all PASS.
* Without planting, the campaign's own save: FAIL (fix off) 20261118-002353, PASS (fix on) 20261118-002106 (probe
  versions of the test, lane 2).

## History
- 2026-11-18: opened.
- 2026-11-18: root cause: FormTribe's index min(size, tribes - 1) past a species list shorter than its property list
  allows (the campaign's planet: 4 of 5); the game's own update faulted every second, the test command crashed the
  host. Fixed with `tribe.form_species` on fix/w52-formtribe; FAIL 20261118-002824, PASS 20261118-002819. BUG-1238
  opened for the village that never forms there.
- 2026-11-18: BUG-1238 on the same branch: the villages after the list take its last species (the hold stays as
  `tribe.form_last_species off`); tribe_form_species checks both (FAIL 20261118-005109 with tribe.form_species off).
- 2026-11-18: released in 0.7.15.
