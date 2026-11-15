---
id: BUG-0892
title: Cell: no test checks that a guest's poison clouds hurt anything; cell_poison passes on the mod's cloud counter
status: fixed
kind: test
stage: Cell
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-small
commits: aff0790d 9d537f59 4cca6fc0
tests: cell_poison
signature:
docs: docs/known-issues.md#issue-cell-guest-poison-damage, docs/06-testing.md#testing-p1-g1, docs/re/cell.md#cell-poison-damage
related: BUG-0716, BUG-0428, BUG-0715, BUG-0799, BUG-0835
---

## Report
A guest's poison part may still do no damage to the host's creatures. Its clouds are now made in the host's game, but no run has shown them taking health off a creature; the test passes as long as the host's game makes clouds at all.

Severity: medium. Confidence: high.

## Root cause
* Observed (code): tools/tests/coop_cell_parts_attack.py:108, for --part poison the only check is `puppet_clouds > 0`, a mod counter incremented at mods/coop/cellsync.cpp:753.
* Observed (code): tools/tests/coop_cell_parts_attack.py:111-112, for poison the creature's health is only printed (the electric case asserts it at :110).
* Observed (code): tools/tests/suite.py:319 says "they hurt the host's creatures", which the test does not check.
* Observed (ticket): BUG-0716 "Not verified: the clouds' damage": one run lost 2 hp, the other none; with the grafted model data even the host's own avatar's clouds hurt nothing. Only docs/known-issues.md#issue-cell-guest-poison-damage records it, no ticket.
* Inferred (medium): the graft writes only model data +0x1270..+0x1278; a real poison part carries more.

## Fix idea
Find what a real poison part carries (Ghidra on the poison step 0xE76880 and the cloud's damage path), then give the test a real poison part (a pool cell that has one, or a creation with the part) instead of the graft.

## How to test
cell_poison with a real poison part: the host creature next to the guest's clouds must lose health and die in the HOST's game (game hp and dead flag), and the guest's mirror must show the same. Must FAIL with SPORE_FIX_OFF=cell.puppet_poison and PASS with the fix; the counter must not be the pass condition.

## Fix
No mod change for players (a test fix); the damage is real. cell_poison (`coop_cell_parts_attack.py --part poison`) now judges the creature's health in the host's game: the host's game makes the guest's clouds from the guest's creation (the graft in the puppet's model data, `puppet_poison`), the creature is put onto each new cloud near the guest's puppet (`coop.pool place c<key> c<cloud>`, host, new: the game's own contact pair does the rest), its hp must drop in the host's game and the guest's mirror must show the loss. The fixture's host cell shares the model (and so the graft): `coop.pool noowncloud 1` (host, tests, new) keeps the host's own avatar from making clouds, so every cloud is the guest's. The cloud counter is printed only. The earlier "no damage" (lab 20261108-044202) had the creature only next to the clouds; a creature does not swim into them by itself in 40 s. A real poison part (Ghidra on the cloud's damage) was not needed: the grafted part's clouds hurt.

## Verification
* FAIL with SPORE_FIX_OFF=cell.puppet_poison: out/lab/20261112-125843 (no cloud for the guest's puppet, the creature kept 57 hp).
* PASS: out/lab/20261112-125720 (10 clouds of the guest, the creature put onto 7, hp 57 -> 51 in the host's game, the guest's mirror 51), out/lab/20261112-130016 (60 -> 54).
Earlier versions of the test (out/lab/20261112-122554, -124050, -125344) failed for the test's own reasons: the host's cell 45 / 14 m away (the puppet then has no GFX in the host's game and the poison step makes no cloud), and the first noowncloud also held the puppet's clouds (puppet_poison lends it the avatar index).

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0716. Severity medium, confidence high.
- 2026-11-12 (fix/w48-small): the guest's clouds hurt the host's creature (6 hp); cell_poison asserts it (FAIL / PASS).
- 2026-11-12: released in 0.7.13.
