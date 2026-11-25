---
id: BUG-9255
title: GOG 2016 (frozen build): the alpha's galaxy menu planets and era visitors are off
status: fixed
kind: feature
stage: Menu
reported: 2026-11-18
source: verification
role: 
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 88e1dc00
tests: tools/re_lint.py (make test)
signature:
docs:
related:
---

## Report
The 2026-11-18 verification of the alpha: tools/re_lint.py had three problems the live branch does not have. The
alpha uses SporeCoop::GGE_MakePanels, GGE_PlayStep (the galaxy menu shows the galaxy server's planets) and
cOrnament_SetModel (era visitors); the frozen GOG 2016 build has no symbols for them and no line said what is off.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Three lines in data/symbols/frozen_off_922af7c7.txt: both features are off on GOG 2016 (not supported since 0.7.8).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: tooling only; re_lint went from 9 problems to 6 (the six are the live branch's own).
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
