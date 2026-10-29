---
id: BUG-0240
title: the host judges a guest's bite with only part of the game's damage checks (cell type and the hurt lock differ from the host's own bites)
status: open
kind: bug
stage: Cell
reported: 2026-10-28
source: GitHub #174 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
From GitHub issue #174: https://github.com/WoodyWizard/SporeCoop/issues/174 (the full analysis, links and the scenario are there).

BUG-0078 (#56, commits 0af9c83, 9413801, release 0.7.3.0) made the host apply a guest's bite (MSG_CELL_HIT) with DamageCell's gate. The gate copies only part of it. The cell-type part of IsDamageable (only "solid" cells are damageable) and its AI-type / avatar part are missing. The victim's `+0x17C` is read, but a guest bite applied by the mod never sets whatever the game sets after a bite. A guest can therefore damage cells the host's own bite cannot, and may be able to bite faster than the host. Severity: wrong behaviour, minor. The fix must not end up suppressing damage the guest should deal.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
