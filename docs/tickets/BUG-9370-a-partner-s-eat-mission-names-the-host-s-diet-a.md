---
id: BUG-9370
title: A partner's Eat mission names the host's diet (a herbivore told to hunt prey)
status: open
kind: bug
stage: Creature
reported: 2026-11-24
source: deep playtest 2026-11-24
role: guest
versions: 
fixed_in:
branch:
commits:
tests: tools/qa_win (manual, the deep playtest of 2026-11-24)
signature:
docs: docs/known-issues.md#known-partner-eat-mission-diet
related:
---

## Report
Deep playtest 2026-11-24 (Creature, direct session and galaxy planet): the HUD's "Eat" mission of a partner shows
the text of the world's (the host's) diet: a herbivore partner on a carnivore's planet read "You're a carnivore.
Hunt prey to eat.", a carnivore partner on a herbivore's planet "You're an herbivore. Eat a Fruit." Its own creature
eats as its own mouth allows; only the hint misleads. Reproduce: a host whose cell was a carnivore, a partner whose
first creature has a herbivore mouth; look at the partner's mission list. Not analysed (the mission comes with the
host's world).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-24: opened.
