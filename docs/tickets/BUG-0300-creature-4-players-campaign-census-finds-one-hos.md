---
id: BUG-0300
title: Creature, 4 players: campaign census finds one host NPC missing on a guest at 90 m, the edge of the NPC range
status: open
kind: test
stage: Creature
reported: 2026-10-30
source: lab 20261030-060122 campaign_short#3 (fix/w10-cell-4p)
role: guest
versions: fix/w10-cell-4p a964c90
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
campaign_short#3 (lab 20261030-060122, lane 4, after the Cell stage passed): `FINDING [census] Bob: c35: the host has it, the guest not (npc 067B0EFF alive=1 hp=6 ai=1, 90 m from the guest) (creature)`. One NPC exactly at the 90 m boundary the census uses (the host streams NPCs within 110 m, npc_near 90). Seen once; the other two runs of the batch passed. Not analysed (outside fix/w10-cell-4p's Cell task). Likely a check at the boundary (the guest moved, the NPC entered 90 m between the two samples), not a lost NPC.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
