---
id: BUG-0255
title: saves still keep the empty herds made for partners' puppets, and the save filter can reuse an old object list for a second save
status: fixed
kind: bug
stage: Creature
reported: 2026-10-28
source: GitHub #189 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-world
commits: 1337ccf 976af47
tests: save_hygiene_leave (tools/tests/coop_save_hygiene.py --leave), npc_tests test_savestrip_rule
signature:
docs: docs/re/creature.md#creature-save-strip-empty-herds, docs/06-testing.md#testing-w5-world
related:
switch: savestrip.empty_herds, savestrip.per_save
---

## Report
From GitHub issue #189: https://github.com/WoodyWizard/SporeCoop/issues/189 (the full analysis, links and the scenario are there).

**Summary**
BUG-0020 (commits 45acba3, 0d79bae, 2be1123, 3699255, 0a6d447, release 0.7.3.0) keeps our puppets, proxies and their herds out of saves. Two gaps remain in `mods/coop/savestrip.cpp`:
- A herd made for a puppet is stripped only while a puppet is in it. Once the partner leaves or the puppet is rebuilt, the empty herd stays in the world and goes into every save.
- The list of our objects is built again only when the serializer address changes or 0.5 s pass between writes. A second save soon after, through a serializer at the same address, uses the old list.
Severity: minor (no crash known; a herd without an archetype in the player's own save, and a narrow risk of writing a game object as null). Both are incomplete parts of the fix, not regressions against 940fc74. Separately, the 

## Root cause
Observed (lane 5): after the partner left, the herd made for its puppet stayed with `members=0`; the owner rule stripped only herds holding a creature of ours, so the save kept it (`coop.npc archless` 1 after the load). The 0.5 s "new save" rule: by reading (no address reuse shown); a `coop.save` wrote all of its references within one frame (`max_save_frames=0`).

## Fix
* Empty own-kind herds are stripped when surely empty: member list (+0x40/+0x44) readable and empty, no creature of the save points at it (every creature counted), every creature readable (`savestrip_rule.h strip_empty_herd`). The owner guarantee stays: a herd with any creature that is not ours is kept. Switch `coop.fix savestrip.empty_herds`.
* A new list per serializer or per frame (`savestrip_rule.h new_save`), not after a 0.5 s pause; `coop.savestrip` shows `max_save_frames` (the frames one save spans). Switch `coop.fix savestrip.per_save`: its off side is unsafe, but no `COOP_FIX_UNSAFE` exists in this branch (BUG-0214 / #148's registry work): mark it there.
* Not done: the issue's third phase (a recruited game creature in a herd like ours, the partner leaves); creature_desync and save_hygiene cover the owner rule.

## Verification
* FAIL: lab run 20261029-003144 (lane 5, `SPORE_FIX_OFF=savestrip.empty_herds`): save_hygiene_leave failed (1 herd without an archetype in the loaded save).
* PASS: lab runs 20261028-232442, 20261028-235458 (after the merge of main with w5-tools) and 20261029-003648 (the final branch), lane 5.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-world (1337ccf); test save_hygiene_leave: FAIL 20261029-003144 with savestrip.empty_herds off, PASS 20261028-232442, 20261028-235458, 20261029-003648
- 2026-10-29: released in 0.7.3.1.
