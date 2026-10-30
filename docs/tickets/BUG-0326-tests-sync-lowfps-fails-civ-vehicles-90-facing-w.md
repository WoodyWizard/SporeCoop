---
id: BUG-0326
title: tests: sync_lowfps fails 'civ/vehicles: 90% facing within 20 deg' on main d003127 (lane 4, 2 of 2)
status: open
kind: desync
stage: Civ
reported: 2026-10-30
source: fix/w14-creature lab 20261030-163146, 20261030-163902
role: 
versions: 
fixed_in:
branch:
commits:
tests: sync_lowfps
signature:
docs:
related: BUG-0175
---

## Report
`tools/lab.py run sync_lowfps --lane 4` on fix/w14-creature (main d003127 + Creature-only changes):
FAIL "civ/vehicles: 90% facing within 20 deg" in out/lab/20261030-163146 and again with that branch's
switches off (npc.kinematic, creations.editor_after_swap: out/lab/20261030-163902), so not caused by
it. BUG-0175 (fixed) had this suite passing on 2026-10-30 (20261030-133141). Not analysed (Civ is
another agent's area): the facing of the guest's vehicle mirrors at 15 fps.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
