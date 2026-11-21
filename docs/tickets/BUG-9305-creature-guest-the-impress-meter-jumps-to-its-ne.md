---
id: BUG-9305
title: Creature (guest): the impress meter jumps to its new value instead of filling smoothly
status: open
kind: bug
stage: Creature
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9297
---

## Report
Live 67-dev, Creature, a guest (owner, 2026-11-20): "Even when it was the correct one, it snapped the fill, rather than
gradually filling like native Spore."

## Root cause
Not found yet. To check: how the guest's meter is set from the host's value (a jump to the new value) against the
game's own fill (docs/re/creature.md#creature-social-prompts).

## Fix
Planned: the guest sets the meter the way the game fills it (its own animated target), behind a switch.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
