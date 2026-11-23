---
id: BUG-1317
title: Tribe diagnostics: coop.tribe ai reads carry= at the wrong container offset (0.0 while a member carries food)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w57-savestrip
commits: 0db36ea8
tests: tribe_own_species
signature:
docs:
related:
---

## Report
tribe_own_species (lab 20261119-095414, and 20261119-112418 with the member's state printed): "member #2 carries food
when the switch comes on" FAILed: after `coop.tribe order <id> 2 <tree>` the member walked to the tree and stayed in the
gather node 0x0569236E for 60 s while `coop.tribe ai` said `carry=0.0` all along. docs/re/tribe.md#tribe-gather-block had
already noted `carry=` 0.0 while a member carried food home (gather state 19).

## Root cause
Observed (Ghidra 2026-11-19): the gather node sums the carried food with 0xCEE4E0(container) = the bundle vector at
container +4..+8, and the member's container is at member +0x34 (0xCEE490(c+0x34), Citizen_HoldBundle's notes,
docs/re/tribe.md#tribe-carried-bundles). tribe.cpp `carried()` read the vector at member +0x3C..+0x40 (the container
taken at +0x38): always the wrong dwords, 0 in practice. A diagnostic bug only; the game's gathering was not affected.

## Fix
tribe.cpp carried(): the vector at member +0x38..+0x3C (CORRECTION note in the code). No switch: a console read-out
for tests (`coop.tribe ai`), no behaviour.

## Verification
* FAIL: lab 20261119-112418 tribe_own_species (old read-out: `carry=0.0` for 60 s at the tree, the member in the
  gather node). PASS: lab 20261119-113320 and 20261119-114816 (`carry=1.0` after 6 s / 9 s, the food went home).

## History
- 2026-11-19: opened (fix/w57-savestrip, found while fixing tribe_own_species for BUG-1271).
- 2026-11-19: fixed on fix/w57-savestrip.
- 2026-11-23: released in 0.7.16.
