---
id: BUG-1266
title: Cell: the host's electric zap is drawn on the guest's screen as a trail of giant white rings
status: open
kind: bug
stage: Cell
reported: 2026-11-18
source: key-frame review
role: guest
versions: 0.7.15.1
fixed_in:
branch:
commits:
tests: cell_attack_electric
signature:
docs:
related: BUG-1243
---

## Report
Key-frame review of the 0.7.15.2 final check (lab 20261118-144132, sheet
`frames/sheets/007_cell_attack_electric_01_host_zaps.jpg`): while the host's cell zaps with an electric part, the
host's own screen shows a small yellow spark, the guest's screen shows a trail of giant white rings centred on the
host's cell, covering half the screen. The same picture is in the frames of rel/0.7.15.1 (20261118-110806) and
fix/w52-evoeditor (20261118-102744): older than 0.7.15.2. The test's checks (animations) pass; only the picture
shows it.

Reproduce: `tools/lab.py run cell_attack_electric`, then `tools/frames_review.py <run> --test cell_attack_electric`.

## Root cause
Not analysed yet. Guess: the zap effect relayed to the guest is created at the puppet with a wrong scale (cell
effects scale with the cell's size) or as a different effect id.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened from the key-frame review of the 0.7.15.2 final check; seen since 0.7.15.1 at least; not a blocker of 0.7.15.2 (visual only, pre-existing), planned for 0.7.16.
