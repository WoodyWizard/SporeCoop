---
id: BUG-0689
title: creature_brain_hp: the test expects the guest to play its own creature, but since BUG-0345 a guest plays the world's species in a world it never played its own in
status: open
kind: test
stage: Creature
reported: 2026-11-07
source: fix/w46-p4-creature regression run 20261107-212113
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
creature_brain_hp FAIL "the guest plays its own creature (not the world's player species)" on lanes 4 and 5:
fix/w46-p4-creature out/lab/20261107-212113 and 20261107-215918, and the rel/0.7.11 base 2577101f
(out/lab/20261107-220638, fix/w46-p4-creature tmp worktree). The other checks pass (both games at brain level 1, the
max health rises by the same step, each game shows the other's). The guest log says "own=065CBE70 from this world's
record (worlds.ini)": since BUG-0345 / BUG-0638 a guest plays its own creation only in a world it played (or chose) it
in, so the test's precondition no longer holds; its last pass was 2026-11-01 (fix/w30-creature-hp), before those
changes. Likely the test must choose the guest's creation in this world first (`coop.creations choose creature <id>`)
before the swap it expects. Not a player-facing bug (inferred). Reproduce: `tools/lab.py run creature_brain_hp`.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
