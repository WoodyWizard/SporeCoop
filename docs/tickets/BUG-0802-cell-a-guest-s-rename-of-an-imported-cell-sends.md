---
id: BUG-0802
title: Cell: a guest's rename of an imported cell sends only its metadata and the host crashes at 0xE83EC8
status: open
kind: crash
stage: Cell
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 BUG-0455 (commit b3d24b5, lineages30-dev)
role: host guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature: 3010a413:0xE83EC8 read 0x9C (CreateCellObject for a partner's cell after its editor return)
docs: docs/re/creations.md
related: BUG-0455
---

## Report
Found by the fork (two-PC Steam session, 2026-11-08), not by our players yet. A guest opens the Cell editor
on a cell it got from somewhere else (an imported / partner cell), only changes the name and saves. The host's
game crashes at 0xE83EC8 when it builds the guest's new cell (the same signature as BUG-0455).

## Root cause
* Observed by the fork: after a free rename of an imported cell, the guest's EditorSaves.package holds only the
  new metadata record (0x030BDEE3); the model (bem 0x1A99B06B) stays in the mounted package it came from. The
  bundle the guest sends has records, but no model. The host registers it under a remapped key that has no
  model, and native Cell puppet creation faults resolving it.
* Our code (inferred from reading, confidence high that the path exists, not reproduced by us):
  * `mods/coop/creations.cpp:2408` (share_own): the BUG-0455 guard only re-reads when `b.recs.empty()`; a bundle
    with metadata but no bem record goes out as it is.
  * `mods/coop/creations.cpp:1743` (apply): a received Cell bundle is applied without checking that it carries
    the cell's bem record.
  * `mods/coop/creations.cpp:322` (refresh_options): only bem entries make an option, so a metadata-only
    renamed cell also drops out of the creation choices.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages BUG-0455, commit b3d24b5); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* A helper `has_cell_model(bundle)`: true when a record of type bem with the bundle's own group and instance and
  non-empty data is present.
* apply(): for kind Cell, a bundle with records but no model is refused (`g_last_error = "received Cell creation
  without its model"`), so the receiver keeps the partner's previous appearance (also covers an older sender).
  And an empty alias bundle for the Cell key already applied (`b.recs.empty() && ap.db && b.key == ap.key`)
  returns early so it does not unmount the package that supplies that key.
* share_own(): the re-read condition also fires for `k == kCell && !b.recs.empty() && !has_cell_model(b)`.
  After make_bundle, if the model is still missing, read the bem records of `{instance, bem, group}` from the
  game's mounted resources (`records_from_game`), else from the kept old records (`g_old_recs`, BUG-0370), and
  append them; if there is still no model, `g_waits_on_disk++` and try next round (never send).
* refresh_options(): for Cell, a metadata entry whose bem is not in EditorSaves but exists in the game
  (`key_exists`) or in `g_old_recs` also makes an option.
* own_key_here(): for Cell, when our applied own key maps to the original key and only the cached model has it,
  use the applied key.
* Their test: a real guest editor rename on two PCs; before: one metadata record sent, host fault at 0xE83EC8;
  after: two consecutive renames with two records each, zero faults, the guest kept its DNA and world.

## How to test
Lab, 2 instances, Cell: the guest imports a cell (or takes one from the host's world), opens its Cell editor,
changes only the name, saves. FAIL before: `coop.creations` on the guest shows a sent Cell bundle with 1 record
(metadata only) and the host logs a fault at 0xE83EC8 (or a refused / modelless puppet). PASS after: the bundle
has the bem and metadata records (2+), the host shows the renamed cell, zero faults; with the switch off the old
behaviour returns.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (code audit, not reproduced by us).
