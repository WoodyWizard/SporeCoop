---
id: BUG-1156
title: E4: Tribe: the host's new tribe outfit never reaches a guest kept in place after the outfitter
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-15
source: plan
role: guest
versions: 0.7.14
fixed_in: unreleased
branch: fix/w51-editors
commits: cebe2441, ea6cf28b
tests: tribe_outfit
signature:
docs: docs/re/creations.md#creations-stage-designs, docs/re/tribe.md#tribe-outfitter
related: BUG-0508, BUG-0200, BUG-1158
---

## Report
Playability audit of wave E (out/audit-0.7.15/playability/tribe_civ.md, item 2), confirmed in the lab: the host edits
its tribe's outfit in the Tribal Outfitter (hut planner, second tab, the card's edit badge). Since BUG-0508 the guest
stays in place after the host's editor, and its tribe kept the old look for good (until the next world from the
host), while the notice said "Alice evolved the species: everybody gets the new body".

## Root cause
Observed (lane 4, base build of rel/0.7.15 c90f2ed9, by hand; then the FAIL twin): the outfitter saves a new creation
in the host's My Creations (2923BFD6, parent 067B3E4A) and reloads the tribe's species profile in place (cTribeUI
0xCDCC90 -> 0x4DFBC0 -> SpeciesProfile_Load 0x4D4F70, every member rebuilt). The host's profile +0x504 was 2923BFD6,
the guest's stayed 067B3E4A: nothing in mods/coop sent the tribe's species or the creation
(docs/re/creations.md#creations-stage-designs).

## Fix
designs.cpp, wsync row `tribe_outfit` (id 16; older versions skip it): the host's tribe profile key (and model key)
and its creation (creations::host_species_out, registered on the guest under 0x7D...... with the BUG-0200 alias).
A guest loads its tribe's profile in place from it outside the frame and rebuilds every member as the game's
outfitter return does; +0x504 keeps the host's key (a save's main stream names it), +0x510 is ours while playing
and the host's while a save is written and after the session. The host's vote and return notices now speak of the
tribe's outfit. Switch: `coop.fix tribe.outfit_sync off`.

## Verification
tribe_outfit (real input: the host's outfitter, the guest's I'm ready; checks the guest's tribe profile key, the bem
hash of its model = the host's, every member's generation, no key of ours in the guest's save):
FAIL with SPORE_FIX_OFF=tribe.outfit_sync,editors.guest_designs: out/lab/20261115-131200;
PASS (after the merge of rel/0.7.15 5901c362): out/lab/20261115-133408 (two runs each, play_tribe and play_civ --quick 2x PASS in the same run); before it: out/lab/20261115-131752 (two runs each; civ_host_building, civ_host_design, tribe_editor_world 2x PASS in the same run). Earlier PASS runs: out/lab/20261115-130639 (tribe_outfit), out/lab/20261115-130858 (civ_designs).
Not verified: a host's outfit made while no guest was in the session (the row covers it after the load, not run).

## History
- 2026-11-15: opened.
- 2026-11-15: reproduced and fixed in fix/w51-editors.
