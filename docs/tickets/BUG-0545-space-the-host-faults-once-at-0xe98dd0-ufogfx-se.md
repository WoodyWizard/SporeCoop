---
id: BUG-0545
title: Space: the host faults once at 0xE98DD0 (UfoGfx SetVisible, model null) in coop_space_diplomacy.py
status: duplicate
kind: crash
stage: Space
reported: 2026-11-04
source: lab 20261104-113235 (fix/w42-space-play, lane 2, GOG 2024)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at 0x00E98DD0 (SporeCoop::UfoGfx_SetVisible+0x40) data 0x4, host
docs:
related: BUG-0323, BUG-0546
---

## Report
`coop_space_diplomacy.py` (fixture space_empire) on lane 2: one caught fault on the HOST 18.6 s after the join,
`0xC0000005 at 0x00E98DD0 SporeCoop::UfoGfx_SetVisible+0x40 (data 0x00000004)`, eax/esi 0, calls 00E9AE74
00E9290C 00E9294D 00E9B217 00E9950B 00B37D2E 00B3687C 00C37003 00BD790D 00AC1F2B: the BUG-0323 signature (a ship
without a model made visible), but on the host, where BUG-0323's fix (the guest's mirror model check) does not
apply; probably a guest puppet whose model the host does not have. The rerun passed (lab 20261104-114437).
Not caused by fix/w42-space-play's changes as far as seen (they do not touch ships). Not analysed.

## Root cause
Duplicate of BUG-0546 (the same host fault in space_diplomacy, the same call chain 00E9AE74 00E9290C ... 00B3687C): the game's own null ship model for an NPC empire ship whose creation the host's Sporepedia cache has without its model record (docs/re/space.md#space-ufo-null-model), not a guest puppet.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
- 2026-11-04: closed as a duplicate of BUG-0546 (fix/w42-infra).
