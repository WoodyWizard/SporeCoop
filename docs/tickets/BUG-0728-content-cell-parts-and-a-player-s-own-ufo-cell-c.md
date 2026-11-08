---
id: BUG-0728
title: Content: cell parts and a player's own UFO / cell creation through the lookalike untested
status: fixed
kind: bug
stage: Cell
reported: 2026-11-08
source: BUG-0424 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content-b
commits: e896b4a8, 2fe31954
tests: content_own_cell, content_own_ufo (coop_content_own_lookalike.py)
signature:
docs: docs/re/creations.md#creations-k2b-design-content, docs/06-testing.md#testing-k2b-content
related: BUG-0424, BUG-0559, BUG-0725
---

## Report
K2b audit of the class BUG-0424 (BUG-0559's open point): the lookalike of a part the receiving game lacks was tested for creature parts only (content_lookalike); cell parts and a ship's parts were never tested through it.

## Root cause
Untested paths. Observed: the cell path worked as built (a player's cell bundle carries its parts' descriptions, add_part_descs knew the cell type); the ship path of a host's design had no descriptions before BUG-0725 (the FAIL run of the switch content.lookalike shows the fixed fallback).

## Fix
Tests: `content_own_cell` (the guest chooses a cell of its own the host does not have; the host lacks its first part; the host's model of it shows the lookalike) and `content_own_ufo` (a host's own ship design, BUG-0724, with a part the guest lacks: the guest's mirrors show the lookalike, BUG-0725). No code beyond BUG-0725.

## Verification
FAIL: out/lab/20261108-041301 (content_own_ufo, `SPORE_FIX_OFF=content.lookalike`: no lookalike, a fallback), out/lab/20261108-042148 (content_own_cell, the same switch: no lookalike). PASS: out/lab/20261108-040336 (content_own_cell: C15CFA84 shown as 2921A7A3), out/lab/20261108-042248 (content_own_cell again), out/lab/20261108-035853 (content_own_ufo: 8D53A9D9 shown as 553886FC).
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-065638, PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): tests written; FAIL and PASS runs above.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
