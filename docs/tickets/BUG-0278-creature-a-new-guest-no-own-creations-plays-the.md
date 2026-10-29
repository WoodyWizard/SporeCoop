---
id: BUG-0278
title: Creature: a new guest (no own creations) plays the legless creature from the start of the stage and cannot change it
status: open
kind: bug
stage: creature
reported: 2026-10-29
source: Discord report 2026-10-29
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0178
---

## Report
A player on Discord (2026-10-29, two friends who had just bought the game, version not given, most
likely 0.7.3.0): "we can't really get through phase 2; I spawn with the phase 1 cell without legs;
the host still can play normally; can't change my character".

Known so far (docs/known-issues.md, BUG-0178 item; docs/re/creations.md at the start of the Creature
stage): a guest without own creations gets its own editor on the host's creature as it was before
the host's editor (the legless one made from the cell). Saving that edit makes the pre-legs creature
the guest's own (kept in choices.ini) while the host plays the legged one; nothing offers a way to
change it later. Whether this is what the player hit, or the guest never got the host's evolved
species at all, is open.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
