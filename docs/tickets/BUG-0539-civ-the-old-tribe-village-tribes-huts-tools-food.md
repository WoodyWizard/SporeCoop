---
id: BUG-0539
title: Civ: the old tribe village (tribes, huts, tools, food mats, bundles) stands in the guest's Civ world only
status: fixed
kind: desync
stage: Civ
reported: 2026-11-04
source: divergence detector, play_civ (fix/w42-civ-left, out/lab/20261104-100146)
role: guest
versions: 
fixed_in: 0.7.6
branch: fix/w42-civ-left
commits: f1163897 4bd2a742 cfd1283f 19d9d7e9 7fdfa287 f7f5ca46
tests: coop_civ_tribe_left.py (civ_tribe_left)
signature:
docs: docs/re/civ.md#civ-tribe-raids
related: BUG-0483, BUG-0484
---

## Report
Detector, play_civ on civ_npc (fix/w42-civ-left, out/lab/20261104-100146): `Civilization:noun_only_guest:tribe`,
`tribe_tool`, `tribe_hut`, `tribe_food_mat` (0 in the host's game, 11 in the guest's, whole list) in 2 of 30 Civ
comparisons, and `unmatched:game_bundle` (2 host / 5 guest, 3 guest without a partner) in 3 of 30. BUG-0484 already
placed the bundles at the old tribe village (-234 -34 482). Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_civ`.

## Root cause
Observed (runtime + Ghidra, fix/w42-civ-left): both games start the Civ world with the same 17 old tribes (civ_npc,
with their huts, tools, food mats and 34 bundles). The host's NPC vehicles raid them: a behaviour tree action
(0xDCAAB0, strings "HR: ...": AddMoney 400-1000 or an event, then DestroyInstance of the tribe at 0xDCB2B2; the
village's huts, tools, food mat and bundles go with it). The host's tribe count fell 17 -> 5 in a 10 min play_civ
(out/lab/20261104-125332); a guest's mirrors run no behaviour tree, so the guest destroyed none except at a new
city's site (0xB9BCE6, the founding, on both games). A capture alone removes no tribe.

## Fix
`civ.tribe_ruins`: the host sends the places of its old tribes in Civ (MSG_CIV_STATE chunk 0xFA, every 5 s and 0.5 s
after one was destroyed); a guest destroys a tribe of its own with no host tribe within 10 m on two lists in a row
(DestroyInstance, the game's own last step of the raid). `coop.fix civ.tribe_ruins off`: 0.7.5 behaviour.

## Verification
coop_civ_tribe_left.py (a capture, then the host's game destroys two tribes as its raid does: `coop.civ razetribe`):
FAIL with SPORE_FIX_OFF=civ.tribe_ruins (host 14 tribes, guest 17): out/lab/20261104-130804;
PASS (14 / 14, the guest removed the two and one its host's vehicle raided meanwhile): out/lab/20261104-130500
(two checks of that run were the test's own and were corrected after it: bundles of a capture, a third raid).
Corrected test (the village's own objects; bundles: BUG-0548): FAIL with the switch off out/lab/20261104-135051
(also 20261104-131051), PASS out/lab/20261104-134924.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): opened (split from BUG-0484's bundles).
- 2026-11-04 (fix/w42-civ-left, round 2): analysed and fixed (civ.tribe_ruins).
- 2026-11-04: released in 0.7.6.
