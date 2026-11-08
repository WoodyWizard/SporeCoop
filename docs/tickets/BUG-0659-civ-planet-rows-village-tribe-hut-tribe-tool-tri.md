---
id: BUG-0659
title: Civ: planet rows village, tribe_hut, tribe_tool, tribe_food_mat in organic play_civ, 3-5 rows a run on rel/0.7.9 vs 1 on 0.7.8 (the BUG-0539 raid case passes)
status: fixed
kind: desync
stage: Civilization
reported: 2026-11-06
source: lab log sweep follow-up 2026-11-06 (detector rows rel/0.7.8 vs rel/0.7.9)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in: unreleased
branch: fix/w46-k8-open
commits: a8b022d1
tests: civ_tribe_left_fast, civ_tribe_left, play_civ
signature:
docs: docs/re/civ.md#civ-tribe-ruins-fast
related: BUG-0539
---

## Report
Detector rows Civilization:planet:village / tribe_hut / tribe_tool / tribe_food_mat in play_civ: rel/0.7.8 (out/lab/20261105-*) 1 row of n1 in each of 3 runs; rel/0.7.9 (20261106-*) 3-5 rows of n1-2 in each of 3 runs (n2 in 20261106-200316). BUG-0539 (fixed 0.7.6) verified only the raid case civ_tribe_left (14/14, 20261104-134924). Possible 0.7.9 regression: decided in the 0.7.9 final check by play_civ x3 on v0.7.8 and on rel/0.7.9 on the same lane; if 0.7.9 is worse it blocks the release.

## Root cause
Observed (detector lines of the A/B runs 20261107-014048 in rel/0.7.9 and rel/0.7.8, play_civ x3 each): every
`Civilization:planet:village` row is in ONE sample only (the next sample 20 s later has equal counts, e.g. 16/17 ->
14/14), and the host's count falls between samples (17 -> 4: its NPC vehicles raid the old villages, BUG-0539). So a
row is a raid the guest had not applied yet, not a village left for good. Observed (civ_tribe_left_fast, fix off,
lab 20261108-004412): the guest removes a raided village 5.1-5.3 s after the host's game did. Cause (code, civ.cpp
host_send_tribes / guest_tribes): the guest removes a tribe on its second look without a host tribe within 10 m; the
host sent one list 0.5 s after the raid and the next one 5 s later, so the second look came ~5.5 s after the raid,
in which a 20 s detector sample falls ~25 % of the time (1-3 rows a run, each with the village's hut, tool and food
mat). Real (the guest's player sees the village for 5 s more), small.

## Fix
`civ.tribe_ruins_fast`: after a raid the host sends 3 lists 0.5 s apart (kQuickLists), so the guest's two looks come
within ~1 s (a lost one costs 0.5 s). The guest's two-look rule stays (a list from before a load). Off: one quick
list (0.7.10).

## Verification
civ_tribe_left_fast (`coop_civ_tribe_left.py --fast`: 3 raids by the host's game, `coop.civ razetribe`, the guest's
tribe count every 0.25 s; PASS when it catches up within 2.5 s), lane 4 / 5:
- FAIL with SPORE_FIX_OFF=civ.tribe_ruins_fast: out/lab/20261108-004412 (5.28, 5.20, 5.10 s).
- PASS: out/lab/20261108-004941-2 (x2: 0.57-0.93 s), with civ_tribe_left x2 passing.
- Detector (play_civ, SPORE_DETECTOR=20): see the History line of the final run.

## History
- 2026-11-06: opened from the lab log sweep follow-up; checked in the 0.7.9 final check.
- 2026-11-07 (orchestrator): A/B at the same time, play_civ x3 with SPORE_DETECTOR=20, run 20261107-014048 in both trees: rel/0.7.9 (lane 6) 3/3 OK, the four planet rows of 12/20/8 lines; v0.7.8 (lane 9) 1/3 OK, 12/8/4 lines. Each row is the same n=1 case on both versions (the guest's planet has one village more, with its hut, tool and food mat). Not a 0.7.9 regression; the count varies a lot between runs. Stays open for 0.7.10.
- 2026-11-08: fixed on fix/w46-k8-open (civ.tribe_ruins_fast): the rows were the 5 s window after each raid.
