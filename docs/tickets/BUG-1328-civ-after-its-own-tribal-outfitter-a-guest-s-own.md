---
id: BUG-1328
title: An editor without a creation key cannot be named (no name field, no dice) in a profile with a creation property list under key 0 (lab lane 5)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-19
source: lab
role: 
versions: 0.7.15.4 and older (the game's own end-of-Tribe editors in a profile with the list; code read), 0.7.16 (unreleased)
fixed_in: 0.7.15.5
branch: fix/w58-cityname, fix/w59-hotfix (backport to 0.7.15)
commits: 1b7c74b2 0fb723ca
tests: creation_props_key0, creation_props_key0_off, civ_own_arrival, line_stages_editors
signature: UI::EditorNamePanel +0x11 = 0, +0x20 = 0 (no name field 0xC7CEB1BD, no dice 0x0552C901); a .prop {0, 0x00B1B104, 0x0000E500} in the profile's Pollination.package
docs: docs/re/creations.md#creations-props-key0
related: BUG-1111, BUG-1272, BUG-1276, BUG-1277
---

## Report
Lab, fix/w58-linestages on rel/0.7.16: the guest's own City Hall editor at its Civ arrival (WP-X1, BUG-1276) never
ended on **lane 5**: line_stages_outfit 20261119-165858, 20261119-171852, 20261119-173854, 20261119-180816,
20261119-182732 (left with the red cross) and line_stages_editors 20261119-184827 (no outfit). The campaign's driver
dragged three parts, pressed "done" and the name panel's dice (614, 826), as in every other editor: "You must give your
creation a name before you can save" stayed for 22-25 tries (`coop.civarrival` phase=open, results=0). The guest's
screen (out/lane5/shots/arrival_editor_120.png, stuck_outfit_cityhall*.png, full2.png): the expanded name panel ("Name
your City Hall", Description, Tags) has **no dice** left of the name field; the kit's `text` (WM_CHAR) and key taps do
not reach the name field (the key taps went to the editor's hotkeys: its help page). On **lane 6** the same tests name
the same editor with the dice and end at the 4th "done": line_stages_editors 20261119-164538, 20261119-171852,
20261119-180816, line_stages 20261119-165858 / 20261119-184827, line_stages_nodesign_off 20261119-173854 and
line_stages_outfit 20261119-190903 (with the outfit: so the guest's own Tribal Outfitter is not the cause, as first
thought); lane 2 (fix/w57-own-editors 20261119-162318) too.

## Root cause
Observed (lab lanes 5 and 6, Ghidra, runtime; docs/re/creations.md#creations-props-key0):
* The difference between the lanes is one record of the guest profile's Pollination.package (lane 5, inst10): a
  creation property list `{0x00000000, 0x00B1B104, 0x0000E500}` (119 bytes: the "Name Your Captain" template's
  properties with the name "Breer"). No other lane's profile has a .prop in Pollination.package. Game files, bottles
  (registry, locale), Preferences.prop, login.prop, coop.ini and the guest's creations were compared too: only sizes of
  caches and the mod's per-player store differ, nothing that the name panel reads.
* Editors::cEditor::OnEnter reads the property list of its request's creation (EditorRequest +0x10,
  SporeCoop::CaptainProps_Get at 0x58F9CD). Our own arrival editors and the game's end of Tribe editors carry no
  creation key (0/0/0), so it finds that list under key 0; its non-empty name makes the name read-only
  (UI::EditorNamePanel +0x11 = 0, read at runtime on lane 5) and takes away the dice (+0x20 = 0): no name field, no
  dice, keys go to the editor's hotkeys, "You must give your creation a name" for ever.
* Such a list is what NameCaptainDialog_OnButton writes when the world's species cannot be copied: the captain key is 0
  (BUG-1111) and CaptainProps_Copy / CaptainProps_SetName still save the list, under key 0, into Pollination.package
  (written to the file at once: lab 20261119-200001 left it on lane 6's disk). Inferred source on lane 5: the
  space_captain_none test (uncopyable species, the dice's name) ran on lane 5 on 2026-11-16 and 2026-11-17.
* Can a real player hit it: yes. GitHub #202's reporter had a captain key 0/0/0 (BUG-1111); a player whose "Name Your
  Captain" could not copy the species has this list in the profile for good, and from then on every editor without a
  creation key cannot be saved, also the game's own end-of-Tribe City Hall editor, which has no red cross (the
  player cannot leave Tribe), and a guest's own arrival editors of 0.7.16.

## Fix
`game.creation_props_key0` (mods/coop/creation_props.cpp): a hook on SporeCoop::CaptainProps_Get answers "no list" for
a key whose instance is 0 (no creation has instance 0: a clean profile answers the same). The editors start with an
empty editable name and the dice; the list stays in the file. `coop.creationprops [plant <name> | remove]`: the
guard's count, whether the game has a list under key 0; tests plant one the captain dialog's way and remove it
(DeleteRecord of Pollination.package's database, RemovePropertyList). New symbols: CaptainProps_Copy,
CaptainProps_SetName, NameCaptain_PropsTemplate, cPropManager::GetPropertyList / RemovePropertyList,
UI::EditorNamePanel::Initialize. Test side: the campaign driver's red cross after 14 "done"
(`Campaign.arrival_cancel_after`, fix/w58-linestages) is removed, so a stuck own arrival editor fails line_stages /
line_stages_outfit again.

## Verification
The difference reproduced on lane 6 and the fix (fix/w58-cityname):
- FAIL (no fix, lane 5 as it was): civ_own_arrival 20261119-193851 lane 5 ("the guest left its City Hall editor with a
  creation" FAIL: no dice; panel +0x11 = 0 / +0x20 = 0 read at runtime), while the same build on lane 6 left it
  (20261119-193856).
- FAIL (twin, the difference applied to lane 6): creation_props_key0_off 20261119-200001 lane 6 (late route; planted
  list: no name field, no dice, the key frame shows lane 5's panel); creation_props_key0_off 20261119-202053 lane 5 (final
  route, the live end of Tribe).
- PASS (fix): creation_props_key0 20261119-202049 lane 6 (both editors named with the dice and saved, both results on
  the guest's line, the guard answered 2 reads of key 0); civ_own_arrival 20261119-200005 on lane 5 with its list still
  in the file; line_stages_editors 20261119-200601 on lane 5 (all passed, 11.2 min: the City Hall editor ended at the
  4th "done", as on lane 6), without the campaign's red cross.
- Final tree (acdbda75, rel/0.7.16 merged, nothing new there): creation_props_key0 PASS 20261119-202639 lane 6;
  creation_props_key0_off FAIL 20261119-202705 lane 5 (no name field, no dice) and line_stages_outfit PASS in the same run
  on lane 5 (no red cross any more: the guest's own City Hall editor ended at the 4th "done").
- Lanes 5 and 6 cleaned afterwards (`coop.creationprops remove`: no .prop left in either guest profile's
  Pollination.package).

Backport to the 0.7.15 line (fix/w59-hotfix from main = v0.7.15.4; the fix of fix/w58-cityname without 0.7.16 code; the
0.7.15 line has no own arrival editors, so its test `coop_creation_props_key0.py` drives the game's own end-of-Tribe City
Hall and vehicle editors of the host with the list planted in the host's game; lane 9):
- PASS: out/lab/20261119-204948:creation_props_key0 and again out/lab/20261119-205401:creation_props_key0 (both editors:
  name field and dice shown, named with the dice, saved; the host's game reached Civ; the guard answered 2 reads of key
  0; 0 faults). Key frames cityhall_name_panel / vehicle_name_panel show the empty name field with the dice.
- FAIL (twin): out/lab/20261119-205401:creation_props_key0_off (no name field, no dice in the City Hall editor; its key
  frame shows only the caption "Name your City Hall", Description and Tags: the end of Tribe cannot be left).
- Smoke: out/lab/20261119-205832 lane 9 (13 OK, ui INCONCLUSIVE there as usual off lanes 1-5); ui PASS out/lab/20261119-205419 lane 5.
- Lane 9's host profile cleaned at each test's end (`coop.creationprops remove`: deleted from Pollination.package).

## History
- 2026-11-19: opened (fix/w58-linestages): the lab runs above, the screenshots; first taken for an effect of the guest's own
  outfitter, then seen on lane 5 only (line_stages_outfit passes the editors on lane 6, 20261119-190903); open.
- 2026-11-19: fix/w58-cityname: the lanes compared (bottles, game files, profiles), the list under key 0 found in lane
  5's Pollination.package, the name panel's flags read at runtime and in Ghidra, reproduced on lane 6 by planting the
  list; fixed by `game.creation_props_key0`; the campaign's red-cross workaround removed; fixed. Found on the way:
  BUG-1329 (the late route's City Hall result faults in the Tribe goal listener).
- 2026-11-19: backported to the 0.7.15 line (fix/w59-hotfix): a 0.7.15 player whose profile has the list cannot leave
  Tribe (the game's own City Hall editor); test creation_props_key0 on the end of Tribe.
- 2026-11-19: released in 0.7.15.5.
