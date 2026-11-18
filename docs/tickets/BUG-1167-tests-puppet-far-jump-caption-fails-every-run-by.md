---
id: BUG-1167
title: Tests: puppet_far_jump_caption fails every run by design (BUG-0658's artefact), noise in the extended suite and impact runs
status: fixed
kind: test
stage: Creature
reported: 2026-11-15
source: B1 impact run 20261115-133237
role: 
versions: 0.7.10 .. 0.7.14.1, rel/0.7.15
fixed_in: 0.7.15
branch: feat/w51-npc
commits: 5f283c0d
tests: puppet_far_jump_caption, puppet_far_jump
signature:
docs: docs/06-testing.md#testing-w51-npc, docs/re/creature.md#creature-far-goto
related: BUG-0658, BUG-1138
---

## Report
The B1 impact run picked puppet_far_jump_caption (suite extended) and it failed (20261115-133237 lane 7, rel/0.7.15
20261115-141533): "the host draws the puppet on its ground where the guest is at every place (hop, over m, apart m:
[(2, 5.3, 5.2)])". It failed every run since it was added (2 of 2 before, flaky ledger "fails every run").

## Root cause
By design: the variant was written to show BUG-0658's test artefact (a caption holds the guest's game, its avatar is not
simulated and stays inside the game's collision at hop 2 of the default seed; the host draws its copy 5.2-5.4 m over
GetHeightAt). Its suite comment said "FAILS at hop 2 by design". A test that always fails is noise in every nightly and
impact run. Not a regression: v0.7.14.1 fails the same (hop 2, 4.9 m, 20261115-154901 lane 3), rel/0.7.15 too (4.8 m, lane 2).

## Fix
coop_puppet_far_jump.py --keep-caption (default seed and hops): one check that the artefact is seen at hop 2 (3-7.5 m over
the host's ground) and the usual check for every other hop. A lift at another place, or none at hop 2 (the
explanation of BUG-0658 no longer holds), fails. puppet_far_jump (captions closed) is unchanged. Test-only.

## Verification
FAIL (the old by-design check): 20261115-154901 (v0.7.14.1 lane 3 and rel/0.7.15 lane 2).
PASS: 20261115-194833 (lane 2, the final tree: the artefact at hop 2, the ground elsewhere)

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-npc): the variant checks the documented artefact.
- 2026-11-18: released in 0.7.15.
