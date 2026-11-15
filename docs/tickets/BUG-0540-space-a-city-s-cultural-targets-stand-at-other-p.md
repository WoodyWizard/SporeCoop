---
id: BUG-0540
title: Space: a city's cultural targets stand at other places in each game (campaign_space)
status: fixed
kind: desync
stage: Space
reported: 2026-11-04
source: divergence detector (BUG-0484 split)
role: 
versions: 
fixed_in: 0.7.9
branch: fix/w45-world-objects
commits: c3097c62
tests: host_cultural (tools/tests/coop_world_host.py --case cultural)
signature:
docs: docs/re/civ.md#civ-host-cultural-targets
related: BUG-0484
switch: wobj.cultural_target
---

## Report
Split from BUG-0484: the detector's `unmatched:cultural_target` (noun 0x3D5C325) in campaign_space only (3 / 3, 2
of 3 without a partner within 3 m, nightly 2026-11-03, out/lab/20261103-012624). Not seen in play_civ on
fix/w42-civ-left (out/lab/20261104-100146: no cultural_target row).

## Root cause
Observed: after a fresh load of `civ_npc` (and `civ`) the 4 cultural targets of a city stand at the same places in both
games (lab 20261106-043449); after the guest leaves and joins again 60 s later they are 3.3-8.5 m apart (lab
20261106-043659): the guest's game sets its cities up again from the host's world of then (0xBE7D40) and places them
by its own layout. The campaign_space rows came after a rejoin of the extra guest (out/lab/20261103-012624).

## Fix
Kind `cultural_target` (Civ and Space planets): a guest's target binds to the host's within 3 m, else its nearest
unbound one within 120 m is put at the host's place (SetPosition); a bound one more than 0.5 m off is put back. No make
or removal. Switch `coop.fix wobj.cultural_target off`.

## Verification
host_cultural (civ_npc, a rejoin after 60 s, the targets near the guest compared in both games' object lists):
- FAIL with `SPORE_FIX_OFF=wobj.cultural_target`: lab 20261106-043659 (4 of 4 more than 1 m off).
- PASS: lab 20261106-044559-2 (relocated=8).
- The final test (census centred on the host's first city: around the planet's centre it listed none, lab
  20261106-054127 / -055541 failed before any check of the fix): FAIL with the switch off lab 20261106-055716, PASS lab
  20261106-055718.
- Detector: play_civ had no cultural_target row before or after (lab 20261106-051337); campaign_space (35 min) not
  rerun. ? Whether the city record's copy of the places (+0x618) matters to the AI is not checked.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): opened (split from BUG-0484).
- 2026-11-06 (fix/w45-world-objects): fixed (kind cultural_target), test host_cultural.
- 2026-11-07: released in 0.7.9.
