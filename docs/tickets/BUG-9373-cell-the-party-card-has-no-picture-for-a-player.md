---
id: BUG-9373
title: Cell: the Party card has no picture for a player who never opened the cell editor
status: open
kind: bug
stage: Cell
reported: 2026-11-24
source: deep playtest 2026-11-24
role: guest
versions: 
fixed_in:
branch:
commits:
tests: tools/qa_win (manual, the deep playtest of 2026-11-24)
signature:
docs: docs/known-issues.md#known-cell-party-card-no-picture
related:
---

## Report
Deep playtest 2026-11-24 (Cell): the Party card of a player who never saved a cell of its own (a new game's first
cell, a fresh profile) is an empty frame; after its first cell editor (a mate) the picture is there. Seen on both
games after the host loaded its Cell save again and the guest joined again; in the very first session of the new
game the host's card of the guest had a picture. creations.cpp portrait_tick finds no own cell key to take a picture
from. Cosmetic. Reproduce: a new Cell game on fresh profiles, the guest joins; look at the Party cards.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-24: opened.
