---
id: BUG-1238
title: Tribe: on a planet whose NPC species list is shorter than the tribes it allows (4 species, 5 NPC tribes) the last village never forms and the goal bar stays at 4 of 5
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-18
source: BUG-1233 analysis (final check 20261117-154911)
role: host
versions: 0.7.14.1, unreleased
fixed_in: unreleased
branch: fix/w52-formtribe
commits: 48fb7f20
tests: tribe_form_species, campaign_segment (the final check's Tribe segment)
signature:
docs: docs/re/tribe.md#tribe-form-last-species, docs/re/tribe.md#tribe-form-species, docs/06-testing.md#testing-tribe-form-species, docs/archive/known-issues-2026.md#issue-tribe-species-list-short
related:
---

## Report
Found while analysing BUG-1233 (final check 20261117-154911, campaign_tribe_noclick, 4 players). The campaign's own
planet (Scalinus, made by the stage wizard; its Tribe segment save out/lane3/campaign/20261117-154914/segments/tribe.tgz)
has a property list of 10 for the NPC tribe controller (5 NPC villages, 6 tribes with the player's) but a species list
of only 4. The controller forms 4 NPC villages, then the fifth needs list[4], past the list: before BUG-1233 that read
faulted every second, with `tribe.form_species` alone the call is held. Either way no fifth village forms: the campaign
sat at "no village left to raid (goal bar (4.0, 5.0))" from 16:21 until the host crashed at 16:26; with the hold alone
it would sit there for good: the stage cannot be finished (a player cannot go on). Likely one cause of the
campaign_tribe_noclick flake ("the campaign stopped in tribe", "stage change tribe -> civ did not finish").

## Root cause
Observed (Ghidra, runtime; docs/re/tribe.md#tribe-form-last-species):
* FormTribe's index min(size, tribes - 1) is one past the evident intent min(size - 1, tribes - 1) (BUG-1233): with a
  list shorter than the property list allows there is no species for the villages after it.
* The list is built at the stage's start by 0xCC6160: per archetype pair 0x4D2270 asks the Sporepedia's object template
  DB for 2 candidates, skips the player's species and any without a profile (removing it from the DB), and an archetype
  with none left adds nothing. The keys are kept at +0x7C (`mNPCTribeSpeciesKeys`) and a load rebuilds the list from
  them (0xCC7140), so a short list stays short for the planet's whole stage.
* The game's own list repeats species: in 37 rebuilds (lab 20261118-004440, -005438) 23 had a species two or three
  times. A village with a species another village has is the game's own case.
Not determined: why that planet's list had 4 at its stage start (4-player session, 15:55:33). 33 rebuilds of the same
planet's list in a session gave 5 every time. Negative for the mod: no hook on 0xCC6160 / 0x4D2270 / the OTDB; the one
hooked function used there, cSpeciesManager_PlayerSpecies (BUG-0732), answers its override only inside
cCreatureAnimal_ComputeScale; savestrip filters only the mod's own nouns (citizens, tribes, huts, tools, food mats it
made). Most likely the random pick at the stage's start (both candidates of an archetype unusable), not checked.

## Fix
`tribe.form_last_species` (mods/coop/tribe.cpp `form_last_species` in `h_Form`; host and solo, a guest forms no tribe
(tribe.guest_tribes) and gets the host's villages as copies): when FormTribe would read past the list (BUG-1233's check),
the list is made as long as the tribes for that one call, its last species repeated, in the list's capacity or through
the game's own EASTL insert (`SporeCoop::EASTL_VectorPtr_InsertValue` 0xB96750, which moves the list to new storage as
0xCC7140 does), then its size is put back. The next village takes the list's last species with the archetype of its own
pair of the property list, up to the property list's limit; the game's list and its saved keys stay as the game built
them (nothing new in a save). Off (`coop.fix tribe.form_last_species off`): BUG-1233's hold, no village. The host logs a
short list once: `tribe: the NPC tribe controller has 4 species for 5 NPC villages (property list 10)`.
`coop.tribe form_species` prints the list's keys and the saved key count. Wire bytes unchanged (the guests' copies come
through the existing NPC tribe messages; 0.7.14 guests get them the same way).

## Verification
Test `tribe_form_species` (tribe fixture; list planted to 2 species, no room, 0xFFFFFFFF past it):
* PASS 20261118-005017 (lane 2): tribes 3 -> 6 (the game's own update at 3 / 4, the test command at 5), the list moved to
  new storage (capacity 8), the three new villages with the list's last species, the guest's 6 tribes with the host's
  species, 0 faults on both games, nothing past 6.
* FAIL `SPORE_FIX_OFF=tribe.form_last_species` 20261118-005113 (lane 4): 0 faults, the host stays at 3 tribes for 30 s.
* FAIL `SPORE_FIX_OFF=tribe.form_species` 20261118-005109 (lane 3): faults at 0x406EFF and a host crash (BUG-1233).
Campaign segment (the final check's own Tribe save, Scalinus, 4 players, `lab.py run campaign_segment --segment
out/bug1233/segment.json`, a copy of the run's segment.json pointing at a copy of its save):
* PASS 20261118-005339 (lane 2, 25 min): the host logged `has 4 species for 5 NPC villages`; after the third goal the
  game's own update formed the fifth NPC village with the list's last species (`the village took its last species`,
  01:04:36), the campaign raided villages 1-5, `the goal bar is full (5/5)`, City Hall and vehicle editors, Civ played;
  RESULT OK, no fault, no crash.
* FAIL `SPORE_FIX_OFF=tribe.form_last_species` 20261118-005344 (lane 3, 37 min): `would read its next species past its
  list: no village formed`, villages 1-4 fell, then `no village left to raid (goal bar (4.0, 5.0))` until
  `FAIL stuck tribe/advance: the stage change tribe -> civ did not finish in 30 min (goal bar (4.0, 5.0))`: the
  final check's flake signature, without the fault (BUG-1233's hold).

## History
- 2026-11-18: opened from the BUG-1233 analysis (fix/w52-formtribe).
- 2026-11-18: fixed on fix/w52-formtribe (`tribe.form_last_species`); the cause of the short list at the stage's start
  not found (the mod not involved as far as checked).
