---
id: BUG-0502
title: detector: whole noun lists only on the host (fruit_group, obstacle, node, nest) in editor_desync
status: duplicate
kind: bug
stage: Creature
reported: 2026-11-03
source: detector report of the regression run out/lab/20261103-150248 (fix/w40-crashes)
role: 
versions: 
fixed_in:
branch: fix/w45-one-game-objects
commits:
tests:
signature:
docs:
related: BUG-0596
---

## Report
One detector sample of editor_desync (regression run out/lab/20261103-150248, SPORE_DETECTOR=20, fix/w40-crashes) had whole noun lists in the host's Creature world and none in the guest's: creature 103, citizen 150, herd 133, fruit_group 864, obstacle 512, node 15, nest 103 (+ host_only nest). Seen once (3%); most likely a sample taken while the guest was reloading the host's world (editor_desync switches stages and rejoins), i.e. a detector timing rule rather than a game desync. Not analysed; to check: the guest's stage at that sample, and whether the detector should skip a guest that is loading.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Inferred from the sample's numbers (the guest listed none of the nouns the host listed by the hundred): the guest's
game had no world loaded at that sample (editor_desync reloads the host's world on the guest after each editor).
The same detector rule was made for BUG-0596 in 0.7.8 (tools/tests/worlddiff.py diff: a game that lists no object
while the other lists at least 10 is `apart`, "lists no object (loading)"), with a selftest.

## Fix
Duplicate of BUG-0596 (fixed in 0.7.8): no change here.

## Verification
no-fail-run: duplicate; BUG-0596's selftest (tools_selftest.py worldstate "a guest that lists no object (loading): not compared") covers this sample.

## History
- 2026-11-03: opened.
- 2026-11-06 (fix/w45-one-game-objects, L1): duplicate of BUG-0596 (the detector skips a game that lists no object).
