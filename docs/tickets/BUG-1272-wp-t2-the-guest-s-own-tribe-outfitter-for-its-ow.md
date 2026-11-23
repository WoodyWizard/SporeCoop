---
id: BUG-1272
title: WP-T2: the guest's own Tribe outfitter for its own species
status: fixed
kind: feature
stage: Tribe
reported: 2026-11-18
source: plan-0.7.16
role: 
versions: 
fixed_in: 0.7.16
branch: feat/w54-outfit
commits: 88dd9fee b1783381
tests: tribe_own_outfitter, tribe_outfit
signature: TribeUI_EditorReturn 0x00CDCC90
docs: docs/re/tribe.md#tribe-own-outfitter, docs/06-testing.md#testing-tribe-own-outfitter
related: BUG-1158, BUG-1156, BUG-1270, BUG-1271
---

## Report
Plan 0.7.16 item 1, WP-T2: in the normal co-op mode (creature.mode own) a guest's tribe card edit badge must open the
Tribal Outfitter for the guest's own species; its result dresses the guest's members on every game, not the host's.
In 0.7.15 the guest's outfitter was refused (BUG-1158), and before that (0.7.14) it reloaded the guest's tribe slot
(every member on the guest's screen, the host's too, in the guest's outfit; lost at the next world).

## Root cause
Ghidra (docs/re/tribe.md#tribe-own-outfitter): the planner 0xD130D0 (message 0x53850BAE, the card's badge) builds the
request from the card: key = the species slot's +0x504 (on a guest the host's species), tag 0x116DD1B. The result
message 0x30C11C7 of that tag puts the result in cTribeUI +0x2F8 (0xCD81F0), and the return from the editor 0xCDCC90
(third branch) reloads slot 0 with it and rebuilds every member. Both are local to the game that opened the editor.

## Fix
designs.cpp / vote.cpp: in creature.mode own a guest's outfitter request (editor 0x156276D1, tag 0x116DD1B, host's
world) is taken (`designs::own_outfitter`): its key becomes `species::own_key(Stage::Tribe)`; the hook on
`SporeCoop::TribeUI_EditorReturn` 0xCDCC90 takes the result of our request (key +0x18), releases it and clears +0x2F8
so the slot is not reloaded (the slot stays the host's species); `designs::tick` then calls
`species::set_tribe_outfit(key)` (retried 30 s): WP-0 sends the bundle, every game dresses the guest's members (WP-T1).
BUG-1158's refusal stays for creature.mode host, without the hook and for a request not taken.
Switch: `coop.fix tribe.guest_outfitter off` (the 0.7.15 refusal in every mode).

## Verification
Test `tribe_own_outfitter` (coop_tribe_own_outfitter.py, real clicks: the hut, the card's badge, a body piece, Done),
FAIL twin `SPORE_FIX_OFF=tribe.guest_outfitter`; `tribe_outfit` step 3 (the refusal) now in creature.mode host.
Not run yet: the owner's rule of 2026-11-18 (no lab runs until the night run). Night commands (lanes 4 and 5):
`SPORE_DETECTOR=20 python3 tools/lab.py run tribe_own_outfitter tribe_outfit --lane 4 --lane 5 --retry 0 --no-build`,
`SPORE_FIX_OFF=tribe.guest_outfitter SPORE_DETECTOR=20 python3 tools/lab.py run tribe_own_outfitter --lane 4 --retry 0 --no-build`.
Step 4 needs WP-T1 on `species::key(pid, Stage::Tribe)` (feat/w54-shims).
* PASS: lab 20261119-164857 (fix/w58-savestrip2, the tree of rel/0.7.16 7ac94251 + BUG-1326's save scan)
  tribe_own_outfitter: every check, the host's save names no key of the guest's; key frames 01-03 show the guest's
  members in the leaf outfit and the host's in the tribe's species on both screens.
* FAIL twin: lab 20261119-101851 (rel/0.7.16 worktree) tribe_own_outfitter with SPORE_FIX_OFF=tribe.guest_outfitter:
  "the guest's own Tribal Outfitter opens (0x01654C02, refused 4)" (BUG-1158's refusal); twins _parts_off and
  _result_off FAIL in fix/w57-own-editors 20261119-130049; _keys_off (the save) FAILs in 20261119-164857.
## History
- 2026-11-18: opened.
- 2026-11-18: code, Ghidra RE and the test with its FAIL twin on feat/w54-outfit; stays open until the night run gives the PASS and FAIL run dirs (as BUG-1271, BUG-1274).
- 2026-11-19: fix/w57-own-editors: tribe_own_outfitter: every outfit check PASS on the merged tree (fix/w57-own-editors worktree out/lab/20261119-130049, -142554: back in Tribe, skipped=1, the slot the host's, both games dress the guest's members in its outfit, key frames same look); the only FAIL left is the host save's raw creation key in lastSave.pld / PlanetScripts.pld (BUG-1326; the .spo line key is gone since BUG-1315's merge). Twins tribe_own_outfitter_parts_off (BUG-1308) and _result_off (BUG-1316) FAIL in -130049. Open until BUG-1326.
- 2026-11-19: fix/w58-savestrip2: BUG-1326 was the test's save scan (the host's own planet script id); tribe_own_outfitter PASS in 20261119-164857: fixed.
- 2026-11-23: released in 0.7.16.
