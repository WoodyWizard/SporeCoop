---
id: BUG-1275
title: WP-C3: the guest's own Civ editors (buildings, vehicles, Civ outfitter) for its own line
status: fixed
kind: feature
stage: Civ
reported: 2026-11-18
source: plan-0.7.16
role: 
versions: 
fixed_in: unreleased
branch: feat/w54-civedit
commits: 78a585e5 48c171a0 1b7d5b83 6b93ebd6 2cb5861c
tests: civ_guest_editors, civ_guest_editors_off (twin), civ_designs
signature:
docs: docs/re/civ.md#civ-own-editors, docs/06-testing.md#testing-civ-guest-editors, docs/01-architecture.md#arch-civ-guest-editors
related: BUG-1158, BUG-1270, BUG-1273, BUG-1274
---

## Report
Plan item WP-C3 of docs/plan-0.7.16.md: in own mode the guest's design editors (buildings, land / sea / air vehicles),
its Civ outfitter and its Sporepedia picks in the planner must work for its own species line; until 0.7.15 they were
refused (BUG-1158) and an own pick went back to the host's design.

## Root cause
Ghidra (GOG 2024, docs/re/civ.md#civ-own-editors): the culture set is one per civilization; the game writes it after a
design editor or a pick in the planner's apply 0xD095A0 (Civ_SetDesign + cPlayer +0x1150; called from the planner's
re-open 0xD0E170 with the result the planner kept from message 0x30C11C7, and from the shopper's accept 0xD0CC00) and
in the planner's message 0x5132389 (Civ_SetDesign directly). The design results do not pass CivGame_EditorReturn
0xCFBD70; that function is the Civ mode's state machine, whose state 1 after the editor applies a Civ outfitter's
creature result kept by GameCiv_HandleMessage 0xCFD500 (request tag 0x66787BB): SetPlayerSpecies, Civ_SetSpeciesKey on
every civilization (with their cities' citizens re-synced), cTribe_SetSpeciesSlot on every tribe.

## Fix
`coop.fix civ.guest_editors` (mods/coop/civ_editors.cpp; designs.cpp). On a guest in own mode (creature.mode own,
species.line on) in the host's world: the BUG-1158 Civ refusal is not applied; 0xD095A0 is skipped and its key goes to
species::set_design(design id, key); a 0x5132389 pick's changed entries go back and to the line; the outfitter's result
is handed to cGameModeCiv as cancelled (none of the civilization writes of 0xCFBD70 run) and goes to
species::set_civ_outfit; requests start from the guest's own line keys (SubmitEditorRequest 0xB1DE20); the palette
tiles show the guest's own designs (0xD0C860). designs::civ_sync takes a guest's own culture entry into the line
instead of the "went back" notice. Host mode / switch off: 0.7.15 (refused, went back). New console `coop.civedit`,
`coop.ui cmd <hex command id>`.

## Verification
Owner rule of 2026-11-18: no lab runs until the night run. Test `civ_guest_editors` (PASS expected) and its twin
`civ_guest_editors_off` (SPORE_FIX_OFF=civ.guest_editors, FAIL expected at "the guest's own editor opened"), lanes 6
and 7, on a tree with feat/w54-shims (species::design in civ_own_designs.cpp). Run dirs: pending (night run).
`make -j8`, `make test` pass.
Lab (fix/w57-own-editors, merged trees): before, rel/0.7.16 out/lab/20261119-095414 civ_guest_editors FAIL (its test
compared game.mode wrongly, BUG-1307). PASS whole on the merged tree with fix/w57-civlook's save strip:
fix/w57-own-editors worktree out/lab/20261119-153243 civ_guest_editors (the house and vehicle editors open by real clicks, both designs in the guest's
line under one key on both games, its houses and its bought vehicle drawn from them on both games, the host's culture
set and fields unchanged, no guest key in the host's save). Twin civ_guest_editors_off FAIL fix/w57-own-editors worktree out/lab/20261119-130049 at the
refusal (BUG-1158, 0.7.15). Key frames 01_guest_designs (both screens the guest's new house and vehicle) checked.

## History
- 2026-11-18: opened.
- 2026-11-18: static RE, code, tests on feat/w54-civedit (BUG-1275); lab pending (night run).
- 2026-11-19: fix/w57-own-editors: civ_guest_editors: every editor and look check PASS on the merged tree (fix/w57-own-editors worktree out/lab/20261119-130049, -142554; BUG-1307's test fixes); the only FAIL left is the host save holding the guest's design line keys (Zorispe/Tavael.spo 018EB106 ...), which fix/w57-civlook's BUG-1274 work (not merged yet) is about. Twin civ_guest_editors_off FAIL in -130049 (the refusal). Open until that save strip is in.
- 2026-11-19: fix/w57-own-editors: PASS whole on the merged tree (20261119-153243), twin FAIL (20261119-130049): fixed.
