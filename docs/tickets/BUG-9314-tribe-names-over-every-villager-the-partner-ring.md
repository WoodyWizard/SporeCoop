---
id: BUG-9314
title: Tribe: names over every villager; the partner ring under the unit is enough
status: open
kind: bug
stage: Tribe
reported: 2026-11-21
source: owner 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Owner 2026-11-21: "in tribe stage, I saw text above my units, we shouldn't have that right? Our ring below the unit should be enough."
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
The owner saw name text over the villagers in Tribe (their own and the partner's: draw_owner_tags in coop.cpp draws the owner's name over every member, from before the rings).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
With the partner rings on (tribe.partner_ring, 7K A), draw_owner_tags draws no text; a downed villager keeps its revive ring timer (and the [REVIVE] text only when the ring timer is off). Switch `coop.fix tribe.ring_only off`: the names again. Branch feat/lineages-47.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened.
