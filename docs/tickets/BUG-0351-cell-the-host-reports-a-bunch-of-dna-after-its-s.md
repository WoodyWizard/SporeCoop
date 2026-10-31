---
id: BUG-0351
title: Cell: the host reports 'a bunch of DNA' after its second cell editor (cause not known)
status: open
kind: bug
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (second batch: 11 zips, host and guest)
role: host
versions: 0.7.3.1
fixed_in:
branch: fix/w22-reports2
commits: 6f18d53
tests:
signature:
docs: docs/re/cell.md#cell-editor-dna
related: BUG-0128, BUG-0345
---

## Report
Host mark (19:16:00 host time, Cell): "ive got bunch of DNA (probably a bug)". It came 52 s after
the host's second cell editor of the session (19:14:29-19:15:08, started by the host with 1/2 ready,
2 min 20 s after the first one), while the guest was in its own cell editor and then reloading.

## Root cause
Not known. Observed:
* Host food (S+0x1C) 312 at the first editor, 381 at the second, 405 at the mark (log `cell:` lines);
  the guest was a ghost until 19:12:46 (BUG-0342) and contributed no food before the mark.
* The cell editor's DNA budget is `FUN_00E51410(S+0x1C) - S+0x2C` (Ghidra GOG 2024, 0xE59800
  passes it to the EditorRequest; 0xE51410 interpolates the level table 0x01483B54, 7 ints per
  entry, by food: food 350 -> 25, 400 -> 28, so about 27 at food 381). The mod never writes S+0x2C
  (only S+0x1C/+0x20 through AddFoodPoints).
* Nothing in the logs shows a DNA value in Cell (`dna=-1`; the shared DNA, MSG_DNA, is a Creature+
  thing).
Inferred candidates: (a) the shared food: every player's meals raise every player's food, so the
DNA of both players arrives (by design, never explained to players; after 19:16:08 the guest's
meals were applied on the host, food 405 -> 1000 in 2.5 min); (b) something reset or did not keep
S+0x2C (spent DNA) across the host's evolution / RebuildLevel; (c) the player meant something else.

## Fix
Diagnostics only (fix/w22-reports2, no behaviour change): `cellsync::dna_note` logs
`cell: DNA <when>: food= plant= spent= dna_for_food= budget=` at each cell editor request (h_Submit,
also after the vote) and on entering Cell (a load, an editor's return). `dna_for_food` is the game's
`DNAForFood` 0xE51410 (fastcall, ECX = food), `budget` = dna_for_food - spent, what the editor gets.
The next report with such a mark shows the numbers. Status stays open until then.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-31: opened from the second batch of the 2026-10-31 report zips (host mark).
- 2026-10-31: the diagnostics added on fix/w22-reports2.
